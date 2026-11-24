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

using namespace spectral_util;

static const float kBinHz = kSampleRate / SpectralMode::kFftSize;
static const size_t kPersistentFloats = SpectralMode::kBins * 4;
// Phase advance of bin k per hop, in cycles per bin index.
static const float kExpected =
    static_cast<float>(SpectralMode::kHop) / SpectralMode::kFftSize;
static const float kToBins =
    static_cast<float>(SpectralMode::kFftSize) / SpectralMode::kHop;

bool SpectralMode::Init(Arena* arena) {
  if (!stft_.Init(arena, LeanStft::PER_CHANNEL, this)) return false;
  for (int ch = 0; ch < 2; ++ch) {
    prev_[ch] = arena->Allocate<float>(kFftSize);
    mag_[ch] = arena->Allocate<float>(kBins);
    freq_[ch] = arena->Allocate<float>(kBins);
    syn_phase_[ch] = arena->Allocate<float>(kBins);
    if (!prev_[ch] || !mag_[ch] || !freq_[ch] || !syn_phase_[ch]) {
      return false;
    }
    for (size_t k = 0; k < kBins; ++k) freq_[ch][k] = static_cast<float>(k);
  }
  raw_ = arena->Allocate<float>(kBins);
  out_mag_ = arena->Allocate<float>(kBins);
  out_freq_ = arena->Allocate<float>(kBins);
  tilt_ = arena->Allocate<float>(kBins);
  if (!raw_ || !out_mag_ || !out_freq_ || !tilt_) return false;
  tilt_slope_ = 1000.0f;  // force a rebuild
  UpdateTilt(0.0f);
  morph_mag_[0] = morph_mag_[1] = NULL;
  morph_freq_[0] = morph_freq_[1] = NULL;
  memset(&ctl_, 0, sizeof(ctl_));
  ctl_.texture = 0.5f;
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
        // angle(X * conj(P)): phase advance since the last frame.
        float d = FastAtan2Cycles(im * pr - re * pi, re * pr + im * pi);
        freq[k] = static_cast<float>(k) +
            WrapCycles(d - kExpected * static_cast<float>(k)) * kToBins;
      } else {
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
  const bool passthrough = !hold && !morph && fabsf(ratio - 1.0f) < 1e-3f &&
      snap < 0.001f;

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
      float offset = 0.0f;
      if (snap > 0.001f) {
        int32_t h = RoundToInt(f * inv_f0);
        if (h < 1) h = 1;
        offset = snap * (static_cast<float>(h) * f0_bins - f);
        f += offset;
      }
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

  // ---- Synthesis.
  if (passthrough) {
    for (size_t k = 1; k < kBins; ++k) {
      const float r = raw[k];
      const float g = r > 1e-20f ? out_mag[k] / r : 0.0f;
      s[2 * k] *= g;
      s[2 * k + 1] *= g;
    }
    return;
  }
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
    }
  });
}

size_t SpectralMode::GetPersistentData(PersistentChunk* chunks) {
  chunks[0].data = mag_[0]; chunks[0].size = kBins * sizeof(float);
  chunks[1].data = mag_[1]; chunks[1].size = kBins * sizeof(float);
  chunks[2].data = freq_[0]; chunks[2].size = kBins * sizeof(float);
  chunks[3].data = freq_[1]; chunks[3].size = kBins * sizeof(float);
  return 4;
}

bool SpectralMode::LoadPersistentData(const uint8_t* data, size_t size) {
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
