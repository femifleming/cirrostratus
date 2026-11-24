// Mode 2 — SPECTRAL: spectral freeze / resynthesis.

#include "monsoon/dsp/modes/spectral.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;

static const float kBinHz = kSampleRate / SpectralMode::kFftSize;
static const size_t kPersistentFloats = SpectralMode::kBins * 4;

bool SpectralMode::Init(Arena* arena) {
  if (!stft_.Init(arena, kFftSize, kHop, this)) return false;
  for (int ch = 0; ch < 2; ++ch) {
    mag_[ch] = arena->Allocate<float>(kBins);
    freq_[ch] = arena->Allocate<float>(kBins);
    an_phase_[ch] = arena->Allocate<float>(kBins);
    syn_phase_[ch] = arena->Allocate<float>(kBins);
    if (!mag_[ch] || !freq_[ch] || !an_phase_[ch] || !syn_phase_[ch]) {
      return false;
    }
    for (size_t k = 0; k < kBins; ++k) freq_[ch][k] = static_cast<float>(k);
  }
  out_mag_ = arena->Allocate<float>(kBins);
  out_freq_ = arena->Allocate<float>(kBins);
  if (!out_mag_ || !out_freq_) return false;
  morph_mag_[0] = morph_mag_[1] = NULL;
  morph_freq_[0] = morph_freq_[1] = NULL;
  memset(&ctl_, 0, sizeof(ctl_));
  ctl_.position = 0.0f;
  ctl_.size = 0.0f;
  ctl_.density = 0.0f;
  ctl_.texture = 0.5f;
  prev_freeze_ = false;
  hold_loaded_ = false;
  frozen_ = false;
  grab_ = false;
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

void SpectralMode::Transform(float* spectrum[2], size_t fft_size) {
  (void)fft_size;
  TransformChannel(0, spectrum[0]);
  TransformChannel(1, spectrum[1]);
  grab_ = false;
}

void SpectralMode::TransformChannel(int ch, float* s) {
  const Controls c = ctl_;
  float* re = s;
  float* im = s + kFftSize / 2;
  float* mag = mag_[ch];
  float* freq = freq_[ch];
  float* an_phase = an_phase_[ch];
  float* syn_phase = syn_phase_[ch];
  const bool hold = frozen_ && !grab_;
  const bool grab = grab_;

  // Blur: magnitude retention per frame (density).
  float retain = c.density < 0.5f
      ? c.density * 2.0f * 0.97f
      : 0.97f + (c.density - 0.5f) * 2.0f * 0.025f;
  if (grab) retain = 0.0f;

  // ---- Analysis: magnitudes and instantaneous frequencies.
  const float expected = static_cast<float>(kHop) / kFftSize;  // cycles/bin
  const float to_bins = static_cast<float>(kFftSize) / kHop;
  for (size_t k = 1; k < kBins; ++k) {
    float r = re[k], i = im[k];
    float m = sqrtf(r * r + i * i);
    float ph = FastAtan2Cycles(-i, r);
    float dphi = WrapCycles(ph - an_phase[k] - expected * k);
    an_phase[k] = ph;
    if (!hold) {
      mag[k] = m + retain * (mag[k] - m);
      freq[k] = static_cast<float>(k) + dphi * to_bins;
    }
  }

  // ---- Source spectrum: own, or morphed toward a saved slot. The morph
  // result is written into the (already analysed) spectrum buffers.
  const bool morph = morph_mag_[ch] != NULL;
  const float p = Clamp(c.position, 0.0f, 1.0f);
  const float* src_mag = mag;
  const float* src_freq = freq;
  if (morph) {
    for (size_t k = 1; k < kBins; ++k) {
      re[k] = mag[k] + p * (morph_mag_[ch][k] - mag[k]);
      im[k] = p < 0.5f ? freq[k] : morph_freq_[ch][k];
    }
    re[0] = re[kBins - 1] = 0.0f;
    src_mag = re;
    src_freq = im;
  }

  // ---- Partial thinning (POSITION, only without a morph slot): keep the
  // peaks above a threshold relative to the loudest one, +/- 1 bin.
  const bool thinning = !morph && p > 0.02f;
  memset(keep_, 0, sizeof(keep_));
  if (thinning) {
    float peak = 0.0f;
    for (size_t k = 1; k < kBins; ++k) if (src_mag[k] > peak) peak = src_mag[k];
    // -80 dB at the bottom of the travel, -12 dB at the top.
    float thr = peak * FastExp2((-80.0f + 68.0f * p) * 0.16609640f);
    for (size_t k = 2; k + 1 < kBins; ++k) {
      float m = src_mag[k];
      if (m >= thr && m >= src_mag[k - 1] && m > src_mag[k + 1]) {
        for (size_t j = k - 1; j <= k + 1; ++j) keep_[j >> 5] |= 1u << (j & 31);
      }
    }
  }

  // ---- Shift / snap / tilt. Every peak region moves as a block (keeps
  // the window lobe intact); all its bins take the peak's new frequency.
  const float ratio = SemitonesToRatio(Clamp(c.pitch, -48.0f, 48.0f));
  const float snap = Clamp(c.size, 0.0f, 1.0f);
  const float f0_bins = 65.41f * ratio / kBinHz;
  const float inv_f0 = 1.0f / f0_bins;
  // Tilt: -9..+9 dB/oct around 1 kHz, as an exponent in log2 units.
  const float slope = (Clamp(c.texture, 0.0f, 1.0f) - 0.5f) * 18.0f / 6.0206f;
  const bool tilt = fabsf(slope) > 0.01f;
  const float log2_bin_hz_over_1k = FastLog2(kBinHz / 1000.0f);
  memset(out_mag_, 0, kBins * sizeof(float));
  memset(out_freq_, 0, kBins * sizeof(float));
  float* out_mag = out_mag_;
  float* out_freq = out_freq_;
  const uint32_t* keep = keep_;
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
    for (size_t k = lo; k <= hi; ++k) {
      float m = src_mag[k];
      if (m <= 1e-12f) continue;
      if (thinning && !(keep[k >> 5] & (1u << (k & 31)))) continue;
      int32_t dst = static_cast<int32_t>(k) + d;
      if (dst < 1 || dst >= static_cast<int32_t>(kBins) - 1) continue;
      if (tilt) {
        m *= FastExp2(slope * (FastLog2(static_cast<float>(dst)) +
                               log2_bin_hz_over_1k));
      }
      out_mag[dst] += m;
      out_freq[dst] += m * f;
    }
  });

  // Frequency smear at the top of the DENSITY travel.
  if (c.density > 0.5f) {
    float b = (c.density - 0.5f) * 2.0f * 0.3f;
    float prev = out_mag_[1];
    for (size_t k = 2; k + 1 < kBins; ++k) {
      float cur = out_mag_[k];
      out_mag_[k] = cur + b * (prev + out_mag_[k + 1] - 2.0f * cur);
      if (out_mag_[k] < 0.0f) out_mag_[k] = 0.0f;
      prev = cur;
    }
  }

  // ---- Synthesis phases.
  const bool passthrough = !hold && !morph && fabsf(ratio - 1.0f) < 1e-3f &&
      snap < 0.001f;
  if (passthrough) {
    for (size_t k = 1; k < kBins; ++k) syn_phase[k] = an_phase[k];
  } else {
    const float drift = hold ? 0.004f : 0.0f;
    Random* rng = &rng_;
    ForEachPeakRegion(out_mag_, kBins, [&](size_t lo, size_t hi, size_t pk) {
      float m = out_mag[pk];
      float f = m > 1e-12f ? out_freq[pk] / m : static_cast<float>(pk);
      float ph = syn_phase[pk] + f * expected;
      if (drift > 0.0f) ph += drift * rng->Bipolar();
      LockPhases(lo, hi, pk, ph, syn_phase);
    });
  }
  for (size_t k = 1; k < kBins; ++k) {
    const float m = out_mag_[k];
    const float u = SafeCycles(syn_phase[k]);
    re[k] = m * FastCos(u);
    im[k] = -m * FastSin(u);
  }
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
