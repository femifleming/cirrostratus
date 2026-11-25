// Mode 2 — SPECTRAL: spectral freeze / resynthesis.
//
// Cost notes (Cortex-M4): the FFTs dominate. Per bin, the analysis is one
// sqrt; atan2 runs only on local maxima (one per partial). With neutral
// pitch/snap and no freeze the output keeps the input's own phases (a
// complex gain, no trig at all); otherwise every peak region is shifted as
// a block and phase-locked, so there is one sin/cos pair per partial, not
// per bin.

#include "monsoon/dsp/modes/spectral.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

// Arena::Allocate out of line: Init() calls it many times and flash is
// tight (same allocations, same order).
static __attribute__((noinline)) void* Grab(Arena* arena, size_t bytes) {
  return arena->Allocate(bytes);
}
template<typename T> static inline T* Grab(Arena* arena, size_t n) {
  return static_cast<T*>(Grab(arena, n * sizeof(T)));
}

using namespace spectral_util;

static const float kBinHz = kSampleRate / SpectralMode::kFftSize;
static const size_t kPersistentFloats = SpectralMode::kBins * 4;
// FBK keeps bins below 22.5 kHz as 16-bit fractions of the frame's peak
// (the arena has under 4 KB left).
static const size_t kFbBins = 960;
// Phase advance of bin k per hop, in cycles per bin index.
static const float kExpected =
    static_cast<float>(SpectralMode::kHop) / SpectralMode::kFftSize;
static const float kToBins =
    static_cast<float>(SpectralMode::kFftSize) / SpectralMode::kHop;

bool SpectralMode::Init(Arena* arena) {
  smear_on_ = init_option_ == 1;
  frozen_ = false;
  if (smear_on_) return smear_.Init(arena);
  if (!stft_.Init(arena, LeanStft::PER_CHANNEL, this)) return false;
  for (int ch = 0; ch < 2; ++ch) {
    prev_[ch] = Grab<float>(arena, kFftSize);
    mag_[ch] = Grab<float>(arena, kBins);
    freq_[ch] = Grab<float>(arena, kBins);
    syn_phase_[ch] = Grab<float>(arena, kBins);
    if (!prev_[ch] || !mag_[ch] || !freq_[ch] || !syn_phase_[ch]) {
      return false;
    }
    for (size_t k = 0; k < kBins; ++k) freq_[ch][k] = static_cast<float>(k);
  }
  raw_ = Grab<float>(arena, kBins);
  out_mag_ = Grab<float>(arena, kBins);
  out_freq_ = Grab<float>(arena, kBins);
  tilt_ = Grab<float>(arena, kBins);
  if (!raw_ || !out_mag_ || !out_freq_ || !tilt_) return false;
  // Last, into the ~4 KB the arena has left.
  fb_[0] = Grab<uint16_t>(arena, 2 * kFbBins);
  if (!fb_[0]) return false;
  fb_[1] = fb_[0] + kFbBins;
  fb_scale_[0] = fb_scale_[1] = 0.0f;
  tilt_slope_ = 1000.0f;  // force a rebuild
  UpdateTilt(0.0f);
  morph_mag_[0] = morph_mag_[1] = NULL;
  morph_freq_[0] = morph_freq_[1] = NULL;
  memset(&ctl_, 0, sizeof(ctl_));
  ctl_.texture = 0.5f;
  ctl_.bal = 0.5f;
  frame_ctl_ = ctl_;
  prev_freeze_ = false;
  hold_loaded_ = false;
  frozen_ = false;
  grab_ = false;
  frame_hold_ = false;
  frame_grab_ = false;
  rng_.Seed(0x5bec7a1);
  return true;
}

void SpectralMode::Process(const Controls& c, const FloatFrame* in,
                           FloatFrame* out, size_t n) {
  if (smear_on_) {
    frozen_ = c.freeze;
    smear_.Process(c, in, out, n);
    return;
  }
  ctl_ = c;
  if (c.freeze != prev_freeze_) {
    hold_loaded_ = false;
    prev_freeze_ = c.freeze;
  }
  if (c.trigger) grab_ = true;
  frozen_ = c.freeze || hold_loaded_;
  stft_.Process(in, out, n);
}

// Tilt: -9..+9 dB/oct around 1 kHz (slope is in log2 units per octave).
void SpectralMode::UpdateTilt(float slope) {
  if (fabsf(slope - tilt_slope_) < 0.005f) return;
  tilt_slope_ = slope;
  const float offset = FastLog2(kBinHz / 1000.0f);
  tilt_[0] = 0.0f;
  for (size_t k = 1; k < kBins; ++k) {
    tilt_[k] = FastExp2(slope * (FastLog2(static_cast<float>(k)) + offset));
  }
}

void SpectralMode::TransformChannel(int ch, float* s) {
  if (ch == 0) {
    frame_ctl_ = ctl_;
    frame_grab_ = grab_;
    frame_hold_ = frozen_ && !frame_grab_;
    UpdateTilt((Clamp(frame_ctl_.texture, 0.0f, 1.0f) - 0.5f) * 18.0f /
               6.0206f);
  } else if (frame_grab_) {
    grab_ = false;  // consumed by both channels
  }
  const Controls& c = frame_ctl_;
  const bool hold = frame_hold_;
  float* mag = mag_[ch];
  float* freq = freq_[ch];
  float* prev = prev_[ch];
  float* syn_phase = syn_phase_[ch];
  float* raw = raw_;

  // FBK: per-frame hold of the fed-back spectrum, T60 0.1 s -> 8 s.
  const float fbk = Clamp(c.fbk, 0.0f, 1.0f);
  const bool regen = fbk > 0.001f;
  const bool track = regen && !hold;

  // Blur: magnitude retention per frame (frames are 21 ms apart).
  float retain = c.density < 0.5f
      ? c.density * 2.0f * 0.94f
      : 0.94f + (c.density - 0.5f) * 2.0f * 0.05f;
  if (frame_grab_) retain = 0.0f;

  // ---- Analysis: magnitudes for every bin.
  raw[0] = 0.0f;
  for (size_t k = 1; k < kBins; ++k) {
    const float re = s[2 * k], im = s[2 * k + 1];
    const float m = sqrtf(re * re + im * im);
    raw[k] = m;
    if (!hold) mag[k] = m + retain * (mag[k] - m);
  }
  // Instantaneous frequency of the partials (local maxima) only.
  if (!hold) {
    for (size_t k = 1; k + 1 < kBins; ++k) {
      const float m = raw[k];
      if (m > raw[k - 1] && m >= raw[k + 1]) {
        const float re = s[2 * k], im = s[2 * k + 1];
        const float pr = prev[2 * k], pi = prev[2 * k + 1];
        // With FBK on, a partial that is ending (frame energy below a
        // quarter of the last one's) keeps the pitch measured before: the
        // estimate from a cut-off frame is off by a few Hz, and FBK would
        // sustain it.
        if (regen && 4.0f * m * m < pr * pr + pi * pi) continue;
        // angle(X * conj(P)): phase advance since the last frame.
        float d = FastAtan2Cycles(im * pr - re * pi, re * pr + im * pi);
        freq[k] = static_cast<float>(k) +
            WrapCycles(d - kExpected * static_cast<float>(k)) * kToBins;
      } else if (!regen) {
        // (With FBK on, non-peaks keep the frequency the last output had
        // there, so a sustained partial keeps its exact pitch.)
        freq[k] = static_cast<float>(k);
      }
    }
  }
  memcpy(prev, s, kFftSize * sizeof(float));

  const float ratio = SemitonesToRatio(Clamp(c.pitch, -48.0f, 48.0f));
  const float snap = Clamp(c.size, 0.0f, 1.0f);
  const bool morph = morph_mag_[ch] != NULL;
  const float p = Clamp(c.position, 0.0f, 1.0f);
  const bool tilt = fabsf(tilt_slope_) > 0.01f;
  // BAL: linear shift in bins, exponential in Hz (1 kHz at the ends).
  const float bal = KnobBipolar(c.bal);
  float shift = 0.0f;
  if (bal != 0.0f) {
    shift = (FastExp2(fabsf(bal) * 8.0f) - 1.0f) * (1000.0f / 255.0f / kBinHz);
    if (bal < 0.0f) shift = -shift;
  }
  const bool passthrough = !hold && !morph && fabsf(ratio - 1.0f) < 1e-3f &&
      snap < 0.001f && shift == 0.0f && !regen;

  // ---- Source spectrum: own, or morphed toward a saved slot (written
  // into the spectrum buffer, which the locked path rebuilds anyway).
  const float* src_mag = mag;
  const float* src_freq = freq;
  if (morph) {
    float* mm = s;
    float* mf = s + kBins;
    for (size_t k = 1; k < kBins; ++k) {
      mm[k] = mag[k] + p * (morph_mag_[ch][k] - mag[k]);
      mf[k] = p < 0.5f ? freq[k] : morph_freq_[ch][k];
    }
    mm[0] = 0.0f;
    src_mag = mm;
    src_freq = mf;
  }

  // ---- FBK: the last output comes back as a floor (see Regenerate).
  float fb_energy = 0.0f;
  if (regen) {
    fb_energy = Regenerate(ch, src_mag, fbk);
    src_mag = raw;
  }

  // ---- Partial thinning (POSITION, only without a morph slot).
  const bool thinning = !morph && p > 0.02f;
  if (thinning) {
    memset(keep_, 0, sizeof(keep_));
    float peak = 0.0f;
    for (size_t k = 1; k < kBins; ++k) if (src_mag[k] > peak) peak = src_mag[k];
    // -80 dB at the bottom of the travel, -12 dB at the top.
    const float thr = peak * FastExp2((-80.0f + 68.0f * p) * 0.16609640f);
    for (size_t k = 2; k + 1 < kBins; ++k) {
      const float m = src_mag[k];
      if (m >= thr && m >= src_mag[k - 1] && m > src_mag[k + 1]) {
        for (size_t j = k - 1; j <= k + 1; ++j) keep_[j >> 5] |= 1u << (j & 31);
      }
    }
  }
  const uint32_t* keep = keep_;

  float* out_mag = out_mag_;
  float* out_freq = out_freq_;
  const float* tilt_table = tilt_;

  if (passthrough) {
    // Same bins, same phases: a real gain per bin.
    for (size_t k = 1; k < kBins; ++k) {
      float m = src_mag[k];
      if (thinning && !(keep[k >> 5] & (1u << (k & 31)))) m = 0.0f;
      if (tilt) m *= tilt_table[k];
      out_mag[k] = m;
    }
  } else {
    // Shift / snap: every peak region moves as a block (keeps the window
    // lobe intact); all its bins carry the peak's new frequency.
    memset(out_mag, 0, kBins * sizeof(float));
    memset(out_freq, 0, kBins * sizeof(float));
    const float f0_bins = 65.41f * ratio / kBinHz;
    const float inv_f0 = 1.0f / f0_bins;
    ForEachPeakRegion(src_mag, kBins, [&](size_t lo, size_t hi, size_t pk) {
      float f = src_freq[pk] * ratio;
      float offset = shift;
      if (snap > 0.001f) {
        int32_t h = RoundToInt(f * inv_f0);
        if (h < 1) h = 1;
        offset += snap * (static_cast<float>(h) * f0_bins - f);
      }
      f += offset;
      if (shift < 0.0f && f < 0.5f) return;  // shifted below 0 Hz
      const int32_t d = RoundToInt(static_cast<float>(pk) * ratio + offset) -
          static_cast<int32_t>(pk);
      int32_t dst_pk = static_cast<int32_t>(pk) + d;
      if (dst_pk < 1) dst_pk = 1;
      if (dst_pk > static_cast<int32_t>(kBins) - 1) dst_pk = kBins - 1;
      const float g = tilt ? tilt_table[dst_pk] : 1.0f;
      for (size_t k = lo; k <= hi; ++k) {
        float m = src_mag[k];
        if (m <= 1e-12f) continue;
        if (thinning && !(keep[k >> 5] & (1u << (k & 31)))) continue;
        int32_t dst = static_cast<int32_t>(k) + d;
        if (dst < 1 || dst >= static_cast<int32_t>(kBins) - 1) continue;
        m *= g;
        out_mag[dst] += m;
        out_freq[dst] += m * f;
      }
    });
  }

  // Frequency smear at the top of the DENSITY travel.
  if (c.density > 0.5f) {
    const float b = (c.density - 0.5f) * 2.0f * 0.3f;
    float prev_m = out_mag[1];
    for (size_t k = 2; k + 1 < kBins; ++k) {
      float cur = out_mag[k];
      float v = cur + b * (prev_m + out_mag[k + 1] - 2.0f * cur);
      out_mag[k] = v < 0.0f ? 0.0f : v;
      prev_m = cur;
    }
  }

  if (regen) {
    Remember(ch, tilt, fb_energy);
  } else {
    fb_scale_[ch] = 0.0f;
  }

  const float diffuse = Clamp(c.rvb, 0.0f, 1.0f);

  // ---- Synthesis.
  if (passthrough) {
    for (size_t k = 1; k < kBins; ++k) {
      const float r = raw[k];
      const float g = r > 1e-20f ? out_mag[k] / r : 0.0f;
      s[2 * k] *= g;
      s[2 * k + 1] *= g;
    }
  } else {
    memset(s, 0, kFftSize * sizeof(float));
    const float drift = hold ? 0.006f : 0.0f;
    Random* rng = &rng_;
    ForEachPeakRegion(out_mag, kBins, [&](size_t lo, size_t hi, size_t pk) {
      const float m = out_mag[pk];
      const float f = m > 1e-12f ? out_freq[pk] / m : static_cast<float>(pk);
      float ph = syn_phase[pk] + f * kExpected;
      if (drift > 0.0f) ph += drift * rng->Bipolar();
      ph = WrapCycles(ph);
      const float opposite = WrapCycles(ph + 0.5f);
      const float u = SafeCycles(ph);
      const float cs = FastCos(u);
      const float sn = FastSin(u);
      // Rigid phase locking: neighbours alternate by half a cycle.
      for (size_t k = lo; k <= hi; ++k) {
        const bool odd = ((k - pk) & 1) != 0;
        const float mk = odd ? -out_mag[k] : out_mag[k];
        s[2 * k] = mk * cs;
        s[2 * k + 1] = mk * sn;
        syn_phase[k] = odd ? opposite : ph;
        if (track) freq[k] = f;
      }
    });
  }
  if (diffuse > 0.001f) Diffuse(s, diffuse);
}

// FBK, before the shift: comb = max(source, g * last output) into raw_.
// Returns the comb's energy. Uses raw_, free outside the passthrough path.
float SpectralMode::Regenerate(int ch, const float* src, float fbk) {
  const uint16_t* fb = fb_[ch];
  const float t60 = 0.1f * expf(fbk * 4.3820266f);  // 0.1 * 80^fbk
  const float g = expf(-6.9077553f * (kHop / kSampleRate) / t60) *
      fb_scale_[ch];
  float* comb = raw_;
  comb[0] = 0.0f;
  float e_src = 0.0f, e_fb = 0.0f, e_comb = 0.0f;
  for (size_t k = 1; k < kBins; ++k) {
    const float m = src[k];
    const float r = k < kFbBins ? g * static_cast<float>(fb[k]) : 0.0f;
    const float v = m > r ? m : r;
    comb[k] = v;
    e_src += m * m;
    e_fb += r * r;
    e_comb += v * v;
  }
  // Never more energy than the louder of the input and the decaying tail:
  // shifted copies land in new bins and would otherwise pile up into a
  // ladder much louder than the input.
  const float e_max = e_src > e_fb ? e_src : e_fb;
  if (e_comb > e_max * 1.0001f) {
    const float norm = sqrtf(e_max / e_comb);
    for (size_t k = 1; k < kBins; ++k) comb[k] *= norm;
    e_comb = e_max;
  }
  return e_comb;
}

// FBK, after the shift: keep out_mag_ (without the tilt, which would
// compound on every pass) as 16-bit fractions of its peak. Snap and
// downward transposition merge partials (magnitudes add), so the loop never
// gets more energy back than went in.
void SpectralMode::Remember(int ch, bool tilt, float energy) {
  uint16_t* fb = fb_[ch];
  float* m = raw_;  // the comb is no longer needed
  float peak = 0.0f, e_out = 0.0f;
  for (size_t k = 1; k < kFbBins; ++k) {
    float v = out_mag_[k];
    if (tilt) v /= tilt_[k];
    m[k] = v;
    e_out += v * v;
    if (v > peak) peak = v;
  }
  const float q = peak > 1e-20f ? 65535.0f / peak : 0.0f;
  for (size_t k = 1; k < kFbBins; ++k) {
    fb[k] = static_cast<uint16_t>(m[k] * q + 0.5f);
  }
  float scale = peak * (1.0f / 65535.0f);
  if (e_out > energy) scale *= sqrtf(energy / e_out);
  fb_scale_[ch] = scale;
}

// RVB: phase diffusion, a random rotation of up to +/- half a turn per bin
// (10 random bits, table sin/cos). The locked phases underneath run on, so
// turning RVB down brings the coherent sound straight back. Incoherent
// frames overlap-add ~2.5 dB quieter: made up here.
void SpectralMode::Diffuse(float* s, float amount) {
  const float gain = 1.0f + 0.33f * amount;
  Random rng = rng_;
  for (size_t k = 1; k < kBins; ++k) {
    const int32_t a = static_cast<int32_t>(static_cast<float>(
        static_cast<int32_t>(rng.Next()) >> 22) * amount) & 1023;
    const float cr = kSineTable[(a + 256) & 1023] * gain;
    const float sr = kSineTable[a] * gain;
    const float re = s[2 * k], im = s[2 * k + 1];
    s[2 * k] = re * cr - im * sr;
    s[2 * k + 1] = re * sr + im * cr;
  }
  rng_ = rng;
}

size_t SpectralMode::GetPersistentData(PersistentChunk* chunks) {
  if (smear_on_) return 0;
  chunks[0].data = mag_[0]; chunks[0].size = kBins * sizeof(float);
  chunks[1].data = mag_[1]; chunks[1].size = kBins * sizeof(float);
  chunks[2].data = freq_[0]; chunks[2].size = kBins * sizeof(float);
  chunks[3].data = freq_[1]; chunks[3].size = kBins * sizeof(float);
  return 4;
}

bool SpectralMode::LoadPersistentData(const uint8_t* data, size_t size) {
  if (smear_on_) return false;
  if (size < kPersistentFloats * sizeof(float)) return false;
  const size_t bytes = kBins * sizeof(float);
  memcpy(mag_[0], data, bytes);
  memcpy(mag_[1], data + bytes, bytes);
  memcpy(freq_[0], data + 2 * bytes, bytes);
  memcpy(freq_[1], data + 3 * bytes, bytes);
  hold_loaded_ = true;
  frozen_ = true;
  return true;
}

void SpectralMode::SetMorphSource(const uint8_t* data, size_t size) {
  if (smear_on_) return;
  if (!data || size < kPersistentFloats * sizeof(float)) {
    morph_mag_[0] = morph_mag_[1] = NULL;
    morph_freq_[0] = morph_freq_[1] = NULL;
    return;
  }
  const float* f = reinterpret_cast<const float*>(data);
  morph_mag_[0] = f;
  morph_mag_[1] = f + kBins;
  morph_freq_[0] = f + 2 * kBins;
  morph_freq_[1] = f + 3 * kBins;
}

}  // namespace monsoon
