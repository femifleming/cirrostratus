// Mode 9: CAPTURE (see capture.h, docs/DESIGN.md).

#include "monsoon/dsp/modes/capture.h"
#include "monsoon/dsp/modes/cold_code.h"

#include "monsoon/dsp/stft.h"

namespace monsoon {

static const size_t kLevelSize[CaptureMode::kLevels] = {
  1024, 512, 256, 128, 64, 32, 16, 8
};
static const size_t kLevelOffset[CaptureMode::kLevels] = {
  0, 1024, 1536, 1792, 1920, 1984, 2016, 2032
};
static const uint32_t kCaptureMagic = 0x52545043;  // "CPTR"
static const size_t kCaptureBytes = 48 * 1024;
static const float kVoiceGain = 0.6f;

MONSOON_COLD bool CaptureMode::Init(Arena* arena) {
  table_ = arena->Allocate<int16_t>(kFrames * kFrameSize);
  work_ = arena->Allocate<float>(kCycle);
  spectrum_ = arena->Allocate<float>(kCycle);
  temp_ = arena->Allocate<float>(kCycle);
  if (!table_ || !work_ || !spectrum_ || !temp_) return false;
  if (!capture_.Init(arena, kCaptureBytes, 1)) return false;
  morph_table_ = NULL;
  for (int f = 0; f < kFrames; ++f) {
    BuildDefaultFrame(f);
    period_[f] = 0.0f;
  }
  for (int v = 0; v < kMaxVoices; ++v) {
    phase_[v] = v * 0.137f;
    fm_[v] = 0.0f;
  }
  bal_ = fm_depth_ = 0.0f;
  envelope_ = 0.0f;
  env_stage_ = 0;
  gated_ = false;
  previous_gate_ = false;
  previous_freeze_ = false;
  frozen_ = false;
  build_frame_ = -1;
  build_head_ = 0;
  build_filled_ = 0;
  return true;
}

inline const int16_t* CaptureMode::FramePtr(int frame) const {
  if (frame >= kFrames && morph_table_) {
    return morph_table_ + (frame - kFrames) * kFrameSize;
  }
  if (frame >= kFrames) frame = kFrames - 1;
  return table_ + frame * kFrameSize;
}

// spectrum_ holds a 1024-point ShyFFT spectrum whose bin k is harmonic k.
void CaptureMode::BuildLevels(int frame) {
  const float* re = spectrum_;
  const float* im = spectrum_ + kCycle / 2;
  float scale = 0.0f;
  for (int level = 0; level < kLevels; ++level) {
    const size_t s = kLevelSize[level];
    const size_t h = s / 8;  // 8x oversampled: clean Hermite reads
    memset(work_, 0, sizeof(float) * s);
    for (size_t k = 1; k <= h; ++k) {
      work_[k] = re[k];
      work_[s / 2 + k] = im[k];
    }
    FftInverse(work_, temp_, s);
    if (level == 0) {
      float peak = 0.0f;
      for (size_t i = 0; i < s; ++i) {
        if (fabsf(temp_[i]) > peak) peak = fabsf(temp_[i]);
      }
      scale = peak > 1e-9f ? 0.9f * 32767.0f / peak : 0.0f;
    }
    int16_t* dst = table_ + frame * kFrameSize + kLevelOffset[level];
    for (size_t i = 0; i < s; ++i) {
      float v = temp_[i] * scale;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32767.0f) v = -32767.0f;
      dst[i] = static_cast<int16_t>(lrintf(v));
    }
  }
}

// Sine (frame 0) morphing to sawtooth (frame 15).
MONSOON_COLD void CaptureMode::BuildDefaultFrame(int frame) {
  memset(spectrum_, 0, sizeof(float) * kCycle);
  const float mix = static_cast<float>(frame) / (kFrames - 1);
  float* im = spectrum_ + kCycle / 2;
  for (size_t k = 1; k < kCycle / 2; ++k) {
    float a = mix / static_cast<float>(k);
    if (k == 1) a += 1.0f - mix;
    im[k] = -a;
  }
  BuildLevels(frame);
}

// Normalised square difference (McLeod) on a 2x-decimated segment ending
// at `start` + 2048. Returns the period in samples (48 kHz), or 0.
float CaptureMode::DetectPeriod(int32_t start) {
  float* x = work_;
  for (size_t i = 0; i < kCycle; ++i) {
    int32_t p = start + static_cast<int32_t>(2 * i);
    x[i] = 0.5f * (capture_.At(0, p) + capture_.At(0, p + 1));
  }
  const size_t w = 512;
  const size_t min_lag = 12, max_lag = 500;  // 2 kHz .. 48 Hz
  float* nsdf = temp_;
  float energy0 = 0.0f;
  for (size_t i = 0; i < w; ++i) energy0 += x[i] * x[i];
  if (energy0 < 1e-7f) return 0.0f;
  float best = 0.0f;
  float e = energy0;  // energy of x[lag .. lag + w), updated incrementally
  for (size_t lag = 0; lag <= max_lag; ++lag) {
    if (lag > 0) {
      e += x[lag + w - 1] * x[lag + w - 1] - x[lag - 1] * x[lag - 1];
      if (e < 0.0f) e = 0.0f;
    }
    float r = 0.0f;
    for (size_t i = 0; i < w; ++i) r += x[i] * x[i + lag];
    nsdf[lag] = 2.0f * r / (energy0 + e + 1e-12f);
    if (lag >= min_lag && nsdf[lag] > best) best = nsdf[lag];
  }
  if (best < 0.6f) return 0.0f;
  // First local maximum after the first negative zero crossing that gets
  // within 90% of the best one.
  size_t lag = 1;
  while (lag < max_lag && nsdf[lag] > 0.0f) ++lag;
  for (; lag < max_lag; ++lag) {
    if (lag < min_lag) continue;
    if (nsdf[lag] >= 0.9f * best && nsdf[lag] >= nsdf[lag - 1] &&
        nsdf[lag] >= nsdf[lag + 1]) {
      float a = nsdf[lag - 1], b = nsdf[lag], c = nsdf[lag + 1];
      float denom = a - 2.0f * b + c;
      float shift = fabsf(denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
      return 2.0f * (static_cast<float>(lag) + Clamp(shift, -0.5f, 0.5f));
    }
  }
  return 0.0f;
}

MONSOON_COLD void CaptureMode::BuildFrameFromCapture(int frame) {
  const int32_t head = static_cast<int32_t>(build_head_);
  const int32_t filled = static_cast<int32_t>(build_filled_);
  const int32_t usable = filled - 4200;
  // Frame 0 = oldest audio, frame 15 = newest.
  const float t = (static_cast<float>(frame) + 0.5f) / kFrames;
  const int32_t delay = 2100 + static_cast<int32_t>((1.0f - t) * usable);
  const int32_t start = head - delay;

  float period = DetectPeriod(start);
  period_[frame] = period;
  if (period <= 0.0f) period = static_cast<float>(kCycle);

  // One period resampled to kCycle points, with a crossfade toward the
  // preceding period so the loop point is seamless.
  const float seg = static_cast<float>(start + 1024);
  const float step = period / kCycle;
  float mean = 0.0f;
  for (size_t i = 0; i < kCycle; ++i) {
    float pos = seg + step * static_cast<float>(i);
    float v = capture_.Read(0, pos);
    float fade_start = 0.85f * kCycle;
    if (i >= fade_start) {
      float wgt = (static_cast<float>(i) - fade_start) / (kCycle - fade_start);
      wgt = wgt * wgt * (3.0f - 2.0f * wgt);
      v += (capture_.Read(0, pos - period) - v) * wgt;
    }
    temp_[i] = v;
    mean += v;
  }
  mean /= kCycle;
  for (size_t i = 0; i < kCycle; ++i) temp_[i] -= mean;
  FftDirect(temp_, spectrum_, kCycle);
  BuildLevels(frame);
}

void CaptureMode::Prepare() {
  if (build_frame_ < 0) return;
  BuildFrameFromCapture(build_frame_);
  if (++build_frame_ >= kFrames) build_frame_ = -1;
}

void CaptureMode::Process(const Controls& c, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  if (c.freeze && !previous_freeze_ &&
      capture_.filled() > 8192 + 4200) {
    build_head_ = static_cast<uint32_t>(capture_.write_head());
    build_filled_ = static_cast<uint32_t>(capture_.filled());
    build_frame_ = 0;
  }
  previous_freeze_ = c.freeze;
  if (!c.freeze) {
    for (size_t i = 0; i + kBlockSize <= n; i += kBlockSize) {
      capture_.WriteBlock(in + i);
    }
  }

  // Envelope (ADSR from SIZE). Drones until the first gate arrives.
  if (c.trigger || c.gate) gated_ = true;
  const float s = c.size;
  const float attack = 0.001f + s * s * 1.2f;
  const float decay = 0.05f + s * 2.0f;
  const float sustain = Clamp(s * 2.0f - 0.2f, 0.0f, 1.0f);
  const float release = 0.03f + s * s * 3.0f;
  const bool held = !gated_ || c.gate;
  if (c.trigger || (c.gate && !previous_gate_)) env_stage_ = 1;
  if (!held) env_stage_ = 0;
  previous_gate_ = c.gate;
  const float block_s = static_cast<float>(n) / kSampleRate;
  float env_target;
  float env_coef;
  if (env_stage_ == 1) {
    env_target = 1.2f;
    env_coef = block_s / attack;
  } else if (env_stage_ == 2) {
    env_target = sustain;
    env_coef = block_s / decay * 3.0f;
  } else {
    env_target = 0.0f;
    env_coef = block_s / release * 3.0f;
  }
  if (env_coef > 1.0f) env_coef = 1.0f;
  const float env_start = envelope_;
  float env_end = envelope_ + (env_target - envelope_) * env_coef;
  if (env_stage_ == 1 && env_end >= 1.0f) {
    env_end = 1.0f;
    env_stage_ = 2;
  }
  if (!gated_) env_end = 1.0f;
  envelope_ = env_end;

  // Oscillator setup.
  float f0 = 130.81f * SemitonesToRatio(c.pitch);
  f0 = Clamp(f0, 8.0f, 10000.0f);
  const int voices = 1 + static_cast<int>(Clamp(c.density, 0.0f, 1.0f) * 6.0f + 0.5f);
  const float detune_cents = c.density * 30.0f;
  // BAL: phase distortion. Left bends the cycle (kink at `knee`), right
  // reads the table `sync` times per cycle under a window.
  // Both knobs glide (one-pole per block, ~20 ms) so CV does not jump
  // the read phase.
  const float bal_target = KnobBipolar(c.bal);
  bal_ += 0.05f * (bal_target - bal_);
  if (fabsf(bal_target - bal_) < 1e-4f) bal_ = bal_target;
  const float bal = bal_;
  const float knee = 0.5f + 0.42f * (bal < 0.0f ? bal : 0.0f);
  const float knee_a = 0.5f / knee;
  const float knee_b = 0.5f / (1.0f - knee);
  const float sync = 1.0f + 5.0f * (bal > 0.0f ? bal : 0.0f);
  // Window 1 - a + 5a p(1-p): a hump peaking at 1.25, faded in with BAL.
  const float window = Clamp(4.0f * bal, 0.0f, 1.0f);
  const float win_dc = 1.0f - window, win_hump = 5.0f * window;
  const bool bend = bal < 0.0f, synced = bal > 0.0f;
  // FBK: operator feedback depth in cycles.
  const float fbk = Clamp(c.fbk, 0.0f, 1.0f);
  fm_depth_ += 0.05f * (0.3f * fbk * fbk - fm_depth_);
  if (fbk == 0.0f && fm_depth_ < 1e-5f) fm_depth_ = 0.0f;
  const float fm = fm_depth_;
  const float fm_k = fm * (1.0f / 32767.0f);  // per table unit
  // Mip level: both levels that get mixed must keep every harmonic below
  // 23 kHz, so start one level above the exact limit. TEXTURE lowers the
  // brightness further. Phase distortion reads the table faster (by up
  // to knee_a or sync), so it moves the level up with it.
  const float stretch = (bal < 0.0f ? knee_a : sync) * (1.0f + 2.0f * fm);
  float l0 = log2f(128.0f * f0 * stretch / 23000.0f) + 1.0f;
  if (l0 < 0.0f) l0 = 0.0f;
  float lf = l0 + (1.0f - Clamp(c.texture, 0.0f, 1.0f)) * 5.0f;
  if (lf > kLevels - 1.001f) lf = kLevels - 1.001f;
  const int level = static_cast<int>(lf);
  const float level_frac = lf - level;
  const size_t sa = kLevelSize[level];
  const size_t sb = kLevelSize[level + 1];
  const size_t oa = kLevelOffset[level];
  const size_t ob = kLevelOffset[level + 1];

  const int frames = morph_table_ ? 2 * kFrames : kFrames;
  float fpos = Clamp(c.position, 0.0f, 1.0f) * (frames - 1);
  int fa = static_cast<int>(fpos);
  if (fa > frames - 2) fa = frames - 2;
  const float frame_frac = fpos - fa;
  const int16_t* ta = FramePtr(fa);
  const int16_t* tb = FramePtr(fa + 1);

  for (size_t i = 0; i < n; ++i) out[i].l = out[i].r = 0.0f;

  const float norm = kVoiceGain / sqrtf(static_cast<float>(voices)) / 32767.0f;
  for (int v = 0; v < voices; ++v) {
    float spread = voices > 1 ? static_cast<float>(v) / (voices - 1) * 2.0f - 1.0f : 0.0f;
    float ratio = SemitonesToRatio(spread * detune_cents * 0.01f);
    float inc = f0 * ratio / kSampleRate;
    float pan = spread * Clamp(c.density * 1.5f, 0.0f, 1.0f);
    float gl = (pan > 0.0f ? 1.0f - pan : 1.0f) * norm;
    float gr = (pan < 0.0f ? 1.0f + pan : 1.0f) * norm;
    float phase = phase_[v];
    float fb = fm_[v];
    float env = env_start;
    const float env_inc = (env_end - env_start) / static_cast<float>(n);
    for (size_t i = 0; i < n; ++i) {
      phase += inc;
      if (phase >= 1.0f) phase -= 1.0f;
      // Read phase: self-FM (+0 at zero), then the bend or the sync
      // multiply under its window. Exact at noon.
      float rp = phase + fm_k * fb;
      if (rp < 0.0f) rp += 1.0f;
      if (rp >= 1.0f) rp -= 1.0f;
      float w = 1.0f;
      if (bend) {
        rp = rp < knee ? rp * knee_a : 0.5f + (rp - knee) * knee_b;
      } else if (synced) {
        w = win_dc + win_hump * rp * (1.0f - rp);
        rp *= sync;
        rp -= static_cast<float>(static_cast<int32_t>(rp));
      }
      // Level a.
      float xa = rp * sa;
      int32_t ia = static_cast<int32_t>(xa);
      float fa_t = xa - ia;
      const size_t ma = sa - 1;
      const int16_t* pa0 = ta + oa;
      const int16_t* pa1 = tb + oa;
      float a0 = Hermite(pa0[(ia - 1) & ma], pa0[ia & ma], pa0[(ia + 1) & ma],
                         pa0[(ia + 2) & ma], fa_t);
      float a1 = Hermite(pa1[(ia - 1) & ma], pa1[ia & ma], pa1[(ia + 1) & ma],
                         pa1[(ia + 2) & ma], fa_t);
      float va = a0 + (a1 - a0) * frame_frac;
      float value = va;
      if (level_frac > 0.001f) {
        float xb = rp * sb;
        int32_t ib = static_cast<int32_t>(xb);
        float fb_t = xb - ib;
        const size_t mb = sb - 1;
        const int16_t* pb0 = ta + ob;
        const int16_t* pb1 = tb + ob;
        float b0 = Hermite(pb0[(ib - 1) & mb], pb0[ib & mb], pb0[(ib + 1) & mb],
                           pb0[(ib + 2) & mb], fb_t);
        float b1 = Hermite(pb1[(ib - 1) & mb], pb1[ib & mb], pb1[(ib + 1) & mb],
                           pb1[(ib + 2) & mb], fb_t);
        float vb = b0 + (b1 - b0) * frame_frac;
        value += (vb - va) * level_frac;
      }
      fb = 0.5f * (fb + value);  // table units
      value *= w;
      env += env_inc;
      value *= env;
      out[i].l += value * gl;
      out[i].r += value * gr;
    }
    phase_[v] = phase;
    fm_[v] = fb;
  }
}

size_t CaptureMode::GetPersistentData(PersistentChunk* chunks) {
  header_.magic = kCaptureMagic;
  header_.frames = kFrames;
  header_.frame_size = static_cast<uint16_t>(kFrameSize);
  chunks[0].data = &header_;
  chunks[0].size = sizeof(header_);
  chunks[1].data = table_;
  chunks[1].size = kFrames * kFrameSize * sizeof(int16_t);
  return 2;
}

static const int16_t* ParseSlot(const uint8_t* data, size_t size) {
  if (!data || size < sizeof(CaptureMode::SlotHeader)) return NULL;
  CaptureMode::SlotHeader h;
  memcpy(&h, data, sizeof(h));
  if (h.magic != kCaptureMagic || h.frames != CaptureMode::kFrames ||
      h.frame_size != CaptureMode::kFrameSize) {
    return NULL;
  }
  const size_t off = (sizeof(h) + 3) & ~size_t(3);
  if (size < off + CaptureMode::kFrames * CaptureMode::kFrameSize * 2) {
    return NULL;
  }
  return reinterpret_cast<const int16_t*>(data + off);
}

bool CaptureMode::LoadPersistentData(const uint8_t* data, size_t size) {
  const int16_t* t = ParseSlot(data, size);
  if (!t) return false;
  memcpy(table_, t, kFrames * kFrameSize * sizeof(int16_t));
  build_frame_ = -1;
  return true;
}

void CaptureMode::SetMorphSource(const uint8_t* data, size_t size) {
  morph_table_ = ParseSlot(data, size);
}

}  // namespace monsoon
