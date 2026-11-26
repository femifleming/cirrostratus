// Mode 15 — FALTUNG: convolution with a built-in IR library. See faltung.h.

#include "monsoon/dsp/modes/faltung.h"

#include "monsoon/dsp/modes/faltung_irs.h"
#include "monsoon/dsp/modes/spectral_util.h"
#include "monsoon/dsp/stft.h"

namespace monsoon {

using namespace spectral_util;

static const size_t B = FaltungMode::kBlock;
static const size_t NF = FaltungMode::kFft;
static const int kFadeBlocks = 6;  // 6 x 5.3 ms = 32 ms

__attribute__((cold)) bool FaltungMode::Init(Arena* arena) {
  predelay_ = arena->Allocate<int16_t>(kMaxPredelay);
  if (!predelay_) return false;
  for (size_t p = 0; p < kMaxPartitions; ++p) {
    fdl_[p] = arena->Allocate<int16_t>(NF);
    if (!fdl_[p]) return false;
  }
  fdl_scale_ = arena->Allocate<float>(kMaxPartitions);
  work_ = arena->Allocate<float>(NF);
  spec_ = arena->Allocate<float>(NF);
  acc_ = arena->Allocate<float>(NF);
  prev_ = arena->Allocate<float>(B);
  part_energy_ = arena->Allocate<float>(kNumFaltungIrs * kMaxPartitions);
  if (!fdl_scale_ || !work_ || !spec_ || !acc_ || !prev_ || !part_energy_) {
    return false;
  }
  // Energy of every IR partition (both channels), for BAL's level match.
  for (int ir = 0; ir < kNumFaltungIrs; ++ir) {
    const FaltungIr& f = kFaltungIrs[ir];
    for (size_t q = 0; q < f.partitions; ++q) {
      float e = 0.0f;
      for (int ch = 0; ch < 2; ++ch) {
        const FaltungPartition& p = f.part[ch][q];
        const int16_t* h = kFaltungData + p.offset;
        float sum = 0.0f;
        for (size_t k = 0; k < 2u * p.bins; ++k) {
          sum += static_cast<float>(h[k]) * static_cast<float>(h[k]);
        }
        e += sum * p.scale * p.scale;
      }
      part_energy_[ir * kMaxPartitions + q] = e;
    }
  }
  bal_ = 0.5f;
  bal_peak_ = 1.0f;
  fb_gain_ = fb_left_ = 0.0f;
  for (int i = 0; i < 2; ++i) {
    in_buf_[i] = arena->Allocate<float>(B);
    if (!in_buf_[i]) return false;
    for (int ch = 0; ch < 2; ++ch) {
      out_buf_[i][ch] = arena->Allocate<float>(B);
      if (!out_buf_[i][ch]) return false;
    }
  }
  head_ = 0;
  fill_ = 0;
  parity_ = 0;
  blocks_ready_ = 0;
  blocks_done_ = 0;
  pd_write_ = 0;
  pd_delay_ = 0.0f;
  ir_index_ = 0;
  playing_ir_ = 0;
  fade_from_ = -1;
  fade_blocks_ = 0;
  active_partitions_ = kFaltungIrs[0].partitions;
  trig_offset_ = 0;
  prev_trigger_ = false;
  for (int ch = 0; ch < 2; ++ch) {
    tilt_lp_[ch] = 0.0f;
    filt_ic1_[ch] = filt_ic2_[ch] = 0.0f;
  }
  tilt_coef_ = 1.0f - expf(-kTwoPi * 1000.0f / kSampleRate);
  gain_lo_ = gain_hi_ = 1.0f;
  filt_g_ = 0.5f;
  filt_k_ = 1.414f;
  filt_mode_ = 0;
  frozen_ = false;
  freeze_target_ = 0.0f;
  freeze_gain_ = 0.0f;
  wet_power_ = 0.0f;
  fb_sample_ = 0.0f;
  return true;
}

void FaltungMode::Process(const Controls& c, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  // --- IR choice: POSITION zone + TRIG offset. -----------------------------
  if (c.trigger && !prev_trigger_) trig_offset_ = (trig_offset_ + 1) % kNumFaltungIrs;
  prev_trigger_ = c.trigger;
  int zone = static_cast<int>(c.position * kNumFaltungIrs);
  if (zone < 0) zone = 0;
  if (zone >= kNumFaltungIrs) zone = kNumFaltungIrs - 1;
  ir_index_ = (zone + trig_offset_) % kNumFaltungIrs;

  // SIZE: number of partitions of the current IR.
  const size_t total = kFaltungIrs[ir_index_].partitions;
  size_t parts = 1 + static_cast<size_t>(c.size * (total - 1) + 0.5f);
  if (parts > total) parts = total;
  active_partitions_ = parts;

  // DENSITY: pre-delay (glides, so turning it bends pitch like tape).
  const float pd_target = c.density * static_cast<float>(kMaxPredelay - 2);
  if (fabsf(pd_target - pd_delay_) < 0.5f) pd_delay_ = pd_target;  // settle
  const float pd_inc = (pd_target - pd_delay_) / static_cast<float>(n) * 0.05f;

  // TEXTURE: +/- 6 dB tilt around 1 kHz.
  const float tilt = (c.texture - 0.5f) * 2.0f;
  gain_hi_ = exp2f(tilt);
  gain_lo_ = exp2f(-tilt);

  // PITCH: wet high-cut (CCW) / low-cut (CW).
  if (c.pitch < -0.5f) {
    float hz = 20000.0f * exp2f(c.pitch / 48.0f * 3.74f);   // -> 1.5 kHz
    filt_mode_ = 1;
    filt_g_ = tanf(kPi * Clamp(hz, 20.0f, 21000.0f) / kSampleRate);
  } else if (c.pitch > 0.5f) {
    float hz = 20.0f * exp2f(c.pitch / 48.0f * 5.32f);       // -> 800 Hz
    filt_mode_ = 2;
    filt_g_ = tanf(kPi * hz / kSampleRate);
  } else {
    filt_mode_ = 0;
  }
  filt_k_ = 1.414f;
  const float a1 = 1.0f / (1.0f + filt_g_ * (filt_g_ + filt_k_));
  const float a2 = filt_g_ * a1;
  const float a3 = filt_g_ * a2;

  // FREEZE: hold the wet level reached when FREEZE was pressed.
  const float peak_gain = kFaltungIrs[ir_index_].peak_gain;
  if (c.freeze && !frozen_) {
    freeze_target_ = sqrtf(wet_power_);
    freeze_gain_ = 0.8f / peak_gain;
  }
  frozen_ = c.freeze;
  if (frozen_ && freeze_target_ > 1e-5f) {
    float now = sqrtf(wet_power_);
    float ratio = now > 1e-6f ? freeze_target_ / now : 1.02f;
    freeze_gain_ *= Clamp(ratio, 0.98f, 1.02f);
    freeze_gain_ = Clamp(freeze_gain_, 0.0f, 2.5f / peak_gain);
  }
  const float power_coef = 1.0f / (0.1f * kSampleRate);

  // BAL goes to Prepare(). FBK: loop gain 0.95 x FBK over the left IR's
  // peak gain (x BAL's largest weight), smoothed per block.
  bal_ = c.bal;
  fb_gain_ += 0.2f * (0.95f * Clamp(c.fbk, 0.0f, 1.0f) /
                      (peak_gain * bal_peak_) - fb_gain_);
  if (c.fbk <= 0.0f && fb_gain_ < 1e-4f) fb_gain_ = 0.0f;
  const bool feed = !frozen_ && fb_gain_ > 0.0f;

  for (size_t i = 0; i < n; ++i) {
    float m = frozen_ ? SoftClip(fb_sample_ * freeze_gain_)
                      : 0.5f * (in[i].l + in[i].r);
    if (feed) m += fb_gain_ * fb_left_;
    // Pre-delay line (int16, 2x headroom).
    float q = Clamp(m, -1.99f, 1.99f) * 16383.0f;
    predelay_[pd_write_] = static_cast<int16_t>(q);
    pd_delay_ += pd_inc;
    float rp = static_cast<float>(pd_write_) - pd_delay_;
    if (rp < 0.0f) rp += static_cast<float>(kMaxPredelay);
    size_t i0 = static_cast<size_t>(rp);
    float t = rp - static_cast<float>(i0);
    size_t i1 = i0 + 1 >= kMaxPredelay ? 0 : i0 + 1;
    float d = (predelay_[i0] + (predelay_[i1] - predelay_[i0]) * t) *
        (1.0f / 16383.0f);
    if (++pd_write_ >= kMaxPredelay) pd_write_ = 0;

    in_buf_[parity_][fill_] = d;
    float wl = out_buf_[parity_][0][fill_];
    float wr = out_buf_[parity_][1][fill_];
    fb_sample_ = 0.5f * (wl + wr);
    fb_left_ = wl;
    wet_power_ += power_coef * (fb_sample_ * fb_sample_ - wet_power_);

    float w[2] = { wl, wr };
    for (int ch = 0; ch < 2; ++ch) {
      float x = w[ch];
      tilt_lp_[ch] += tilt_coef_ * (x - tilt_lp_[ch]);
      x = tilt_lp_[ch] * gain_lo_ + (x - tilt_lp_[ch]) * gain_hi_;
      if (filt_mode_) {
        const float v3 = x - filt_ic2_[ch];
        const float v1 = a1 * filt_ic1_[ch] + a2 * v3;
        const float v2 = filt_ic2_[ch] + a2 * filt_ic1_[ch] + a3 * v3;
        filt_ic1_[ch] = 2.0f * v1 - filt_ic1_[ch];
        filt_ic2_[ch] = 2.0f * v2 - filt_ic2_[ch];
        x = filt_mode_ == 1 ? v2 : x - filt_k_ * v1 - v2;
      }
      w[ch] = x;
    }
    out[i].l = w[0];
    out[i].r = w[1];

    if (++fill_ >= B) {
      fill_ = 0;
      parity_ ^= 1;
      ++blocks_ready_;
    }
  }
}

// BAL: a gain per partition, exp2 of a ramp across the active IR (left:
// falling, right: rising), scaled so the IR keeps its energy.
__attribute__((cold)) void FaltungMode::BalWeights(int ir, size_t parts, float bal, float* weight) {
  const FaltungIr& f = kFaltungIrs[ir];
  if (parts > f.partitions) parts = f.partitions;
  const float slope = (bal < 0.0f ? 6.0f : 8.0f) * bal /
      static_cast<float>(parts > 1 ? parts - 1 : 1);
  const float* e = part_energy_ + ir * kMaxPartitions;
  float e0 = 0.0f, e1 = 0.0f;
  for (size_t q = 0; q < parts; ++q) {
    const float w = FastExp2(slope * static_cast<float>(q));
    weight[q] = w;
    e0 += e[q];
    e1 += w * w * e[q];
  }
  const float norm = e1 > 1e-20f ? Clamp(sqrtf(e0 / e1), 0.0f, 16.0f) : 1.0f;
  float peak = 1.0f;
  for (size_t q = 0; q < kMaxPartitions; ++q) {
    weight[q] = q < parts ? weight[q] * norm : 0.0f;
    if (weight[q] > peak) peak = weight[q];
  }
  bal_peak_ = peak;
}

// acc += sum over partitions q of X[head - q] * H[ir][ch][q].
void FaltungMode::Accumulate(int ir, size_t parts, int ch, float* acc,
                             const float* weight) {
  const FaltungIr& f = kFaltungIrs[ir];
  if (parts > f.partitions) parts = f.partitions;
  float* acc_re = acc;
  float* acc_im = acc + B;
  for (size_t q = 0; q < parts; ++q) {
    const size_t slot = (head_ + kMaxPartitions - q) % kMaxPartitions;
    const FaltungPartition& p = f.part[ch][q];
    float s = fdl_scale_[slot] * p.scale;
    if (weight) s *= weight[q];
    // SIZE shorter than the IR: fade the last two partitions out.
    if (parts < f.partitions) {
      if (q + 1 == parts) s *= 0.35f;
      else if (q + 2 == parts) s *= 0.75f;
    }
    if (s == 0.0f) continue;
    const int16_t* x = fdl_[slot];
    const int16_t* xi = x + B;
    const int16_t* hr = kFaltungData + p.offset;
    const int16_t* hi = hr + p.bins;
    const size_t nb = p.bins;
    acc_re[0] += s * static_cast<float>(x[0]) * static_cast<float>(hr[0]);
    if (nb == B) {
      acc_im[0] += s * static_cast<float>(xi[0]) * static_cast<float>(hi[0]);
    }
    for (size_t k = 1; k < nb; ++k) {
      const float a = x[k], b = xi[k], c = hr[k], d = hi[k];
      acc_re[k] += s * (a * c - b * d);
      acc_im[k] += s * (a * d + b * c);
    }
  }
}

void FaltungMode::ProcessAudioBlock() {
  const int p = static_cast<int>(blocks_done_ & 1);

  // New input block into the frequency-domain delay line.
  head_ = (head_ + 1) % kMaxPartitions;
  memcpy(work_, prev_, B * sizeof(float));
  memcpy(work_ + B, in_buf_[p], B * sizeof(float));
  memcpy(prev_, in_buf_[p], B * sizeof(float));
  FftDirect(work_, spec_, NF);
  float peak = 0.0f;
  for (size_t k = 0; k < NF; ++k) {
    float a = fabsf(spec_[k]);
    if (a > peak) peak = a;
  }
  int16_t* dst = fdl_[head_];
  if (peak < 1e-20f) {
    memset(dst, 0, NF * sizeof(int16_t));
    fdl_scale_[head_] = 0.0f;
  } else {
    const float qs = 32767.0f / peak;
    for (size_t k = 0; k < NF; ++k) {
      dst[k] = static_cast<int16_t>(RoundToInt(spec_[k] * qs));
    }
    fdl_scale_[head_] = peak / 32767.0f;
  }

  // IR change: start a crossfade from the IR that was playing.
  const int ir = ir_index_;
  if (ir != playing_ir_) {
    fade_from_ = playing_ir_;
    fade_blocks_ = kFadeBlocks;
    playing_ir_ = ir;
  }
  const size_t parts = active_partitions_;
  const float inv_n = 1.0f / static_cast<float>(NF);
  const float bal = KnobBipolar(bal_);
  float weight[kMaxPartitions], fade_weight[kMaxPartitions];
  const float* w_new = NULL;
  const float* w_old = NULL;
  if (bal != 0.0f) {
    BalWeights(fade_from_ >= 0 ? fade_from_ : playing_ir_, kMaxPartitions,
               bal, fade_weight);
    BalWeights(playing_ir_, parts, bal, weight);
    w_new = weight;
    w_old = fade_weight;
  } else {
    bal_peak_ = 1.0f;
  }

  for (int ch = 0; ch < 2; ++ch) {
    memset(acc_, 0, NF * sizeof(float));
    Accumulate(playing_ir_, parts, ch, acc_, w_new);
    FftInverse(acc_, work_, NF);
    float* o = out_buf_[p][ch];
    for (size_t i = 0; i < B; ++i) o[i] = work_[B + i] * inv_n;
    if (fade_blocks_ > 0 && fade_from_ >= 0) {
      memset(acc_, 0, NF * sizeof(float));
      Accumulate(fade_from_, kMaxPartitions, ch, acc_, w_old);
      FftInverse(acc_, work_, NF);
      // Weight of the new IR rises linearly across the whole fade.
      const float w0 = static_cast<float>(kFadeBlocks - fade_blocks_) / kFadeBlocks;
      const float dw = 1.0f / (kFadeBlocks * static_cast<float>(B));
      for (size_t i = 0; i < B; ++i) {
        float w = w0 + dw * static_cast<float>(i);
        o[i] = o[i] * w + work_[B + i] * inv_n * (1.0f - w);
      }
    }
  }
  if (fade_blocks_ > 0 && --fade_blocks_ == 0) fade_from_ = -1;
}

void FaltungMode::Prepare() {
  if (blocks_done_ == blocks_ready_) return;
  if (blocks_ready_ - blocks_done_ > 1) blocks_done_ = blocks_ready_ - 1;
  ProcessAudioBlock();
  ++blocks_done_;
}

}  // namespace monsoon
