// Mode 22: BARBERPOLE (see barberpole.h).

#include "monsoon/dsp/modes/barberpole.h"

namespace monsoon {

const float BarberSplitter::ka_[4] = {
  0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
  0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f
};
const float BarberSplitter::kb_[4] = {
  0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
  0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f
};

static const float kMinDelay = 72.0f;      // > one block + interpolator
static const float kMaxDelay = 24000.0f;   // 500 ms

bool BarberpoleMode::Init(Arena* arena) {
  if (!loop_.Init(arena, 98 * 1024, 2, 0, buffer_quality_)) return false;
  // A longer buffer (lower buffer quality) gives a longer loop; at 48 kHz
  // 16-bit this is the v1.0 500 ms. Live reads stay behind the newest
  // stored block (guard, 0 at 48 kHz).
  min_delay_ = kMinDelay + static_cast<float>(loop_.guard());
  max_delay_ = kMaxDelay * QualityLengthFactor(buffer_quality_);
  const float room = static_cast<float>(loop_.size()) - 600.0f -
      static_cast<float>(loop_.guard());
  if (max_delay_ > room) max_delay_ = room;
  memset(&k_, 0, sizeof(k_));
  for (int ch = 0; ch < 2; ++ch) {
    splitter_[ch].Init();
    hp_[ch].Init();
    hp_[ch].set(30.0f / kSampleRate, 0.707f);
    lp_[ch].Init();
    lp_[ch].set(14000.0f / kSampleRate, 0.707f);
    phase_[ch] = 0.0f;
    shift_[ch] = 0.0f;
  }
  delay_ = 4800.0f;
  feedback_ = 0.0f;
  input_gain_ = 1.0f;
  direction_ = 1.0f;
  frozen_ = false;
  return true;
}

void BarberpoleMode::Process(const Controls& c, const FloatFrame* in,
                             FloatFrame* out, size_t n) {
  if (c.trigger) direction_ = -direction_;
  frozen_ = c.freeze;

  // Shift: bipolar exponential with a small dead zone at noon.
  float p = (Clamp(c.position, 0.0f, 1.0f) - 0.5f) * 2.0f;
  float mag = fabsf(p);
  float hz = 0.0f;
  if (mag > 0.02f) {
    hz = 0.1f * powf(20000.0f, (mag - 0.02f) / 0.98f);
  } else {
    hz = 0.1f * mag / 0.02f;
  }
  hz *= SemitonesToRatio(c.pitch) * (p < 0.0f ? -1.0f : 1.0f) * direction_;
  hz = Clamp(hz, -4000.0f, 4000.0f);
  const float mix = Clamp(c.texture, 0.0f, 1.0f);
  shift_[0] = hz;
  shift_[1] = hz * (1.0f - 2.0f * mix);
  const float inc[2] = { shift_[0] / kSampleRate, shift_[1] / kSampleRate };

  // Loop delay (smoothed: changing it glides like tape).
  float target = min_delay_ * powf(max_delay_ / min_delay_, Clamp(c.size, 0.0f, 1.0f));
  float d_start = delay_;
  delay_ += 0.1f * (target - delay_);
  if (delay_ > max_delay_) delay_ = max_delay_;
  if (delay_ < min_delay_) delay_ = min_delay_;
  float d_inc = (delay_ - d_start) / static_cast<float>(n);

  float fb_start = feedback_;
  feedback_ = frozen_ ? 1.0f : Clamp(c.density, 0.0f, 1.0f) * 0.98f;
  const float fb_inc = (feedback_ - fb_start) / static_cast<float>(n);
  const float in_target = frozen_ ? 0.0f : 1.0f;

  // BAL / FBK / RVB (FBK moves the read position; BAL and RVB act on the
  // block about to be written into the loop, see LoopFx).
  d_start += Knobs(c, n);
  d_inc += k_.wow_inc;

  FloatFrame stage[kBlockSize];
  const float head = static_cast<float>(loop_.head());
  float d = d_start, fb = fb_start;
  for (size_t i = 0; i < n; ++i) {
    d += d_inc;
    fb += fb_inc;
    input_gain_ += 0.003f * (in_target - input_gain_);
    float pos = head + static_cast<float>(i) - d;
    float y[2];
    for (int ch = 0; ch < 2; ++ch) {
      float delayed = loop_.Read(ch, pos);
      float re, im;
      splitter_[ch].Process(delayed, &re, &im);
      phase_[ch] += inc[ch];
      if (phase_[ch] >= 1.0f) phase_[ch] -= 1.0f;
      if (phase_[ch] < 0.0f) phase_[ch] += 1.0f;
      y[ch] = re * FastCos(phase_[ch]) + im * FastSin(phase_[ch]);
    }
    float x[2] = { in[i].l, in[i].r };
    for (int ch = 0; ch < 2; ++ch) {
      float w = x[ch] * input_gain_ + fb * y[ch];
      w = lp_[ch].Lp(hp_[ch].Hp(w));
      w = SoftClip(w);  // the loop never exceeds full scale
      if (ch == 0) stage[i].l = w; else stage[i].r = w;
    }
    out[i].l = y[0];
    out[i].r = y[1];
  }
  if (k_.loop_fx) LoopFx(stage, n);
  for (size_t i = n; i < kBlockSize; ++i) stage[i].l = stage[i].r = 0.0f;
  loop_.WriteBlock(stage);
}

// FBK: the loop delay is lengthened by up to 2.5 ms (a quarter of the
// delay at most), following a slow LFO (a 0.9 Hz triangle); the
// offset at the start of the block is returned, its ramp goes to wow_inc.
// BAL sets the extra pole, RVB the dispersion; loop_fx says whether LoopFx
// must run.
float BarberpoleMode::Knobs(const Controls& c, size_t n) {
  const float inv_n = 1.0f / static_cast<float>(n);
  float depth = (c.fbk < 1.0f ? c.fbk : 1.0f) * 120.0f;
  if (depth > 0.25f * delay_) depth = 0.25f * delay_;
  float start = 0.0f;
  k_.wow_inc = 0.0f;
  if (depth > 0.0f || k_.wow_depth > 0.0f) {
    start = k_.wow_depth * k_.wow_shape;
    float p = k_.wow + 0.9f / kSampleRate * static_cast<float>(n);
    if (p >= 1.0f) p -= 1.0f;
    k_.wow = p;
    const float s = fabsf(2.0f * p - 1.0f);   // triangle, 0..1
    k_.wow_shape = s;
    k_.wow_inc = (depth * s - start) * inv_n;
  }
  k_.wow_depth = depth;

  // BAL: one-pole coefficient, low-pass 14 kHz -> ~1.2 kHz (0.84 -> 0.15),
  // high-pass 0 -> ~1.5 kHz (0 -> 0.18; the 30 Hz high-pass stays).
  const float bal = KnobBal(c.bal);
  const float b1 = 1.0f + bal;
  k_.bal = bal;
  k_.tone_coef = bal < 0.0f ? 0.15f + 0.69f * b1 * b1 : 0.18f * bal * bal;
  // RVB: the allpass coefficient 0 -> -0.9, blended in over the first 1/8.
  const float rvb = c.rvb < 1.0f ? c.rvb : 1.0f;
  const float mix = rvb < 0.125f ? 8.0f * rvb : 1.0f;
  k_.disp_coef = -0.9f * rvb;
  k_.disp_inc = (mix - k_.disp_mix) * inv_n;
  k_.loop_fx = bal != 0.0f || mix > 0.0f || k_.disp_mix > 0.0f;
  return start;
}

// On the block about to be written into the loop, per side.
// BAL: one more pole, a low-pass closing to ~1.2 kHz (left) or a high-pass
// opening to ~1.5 kHz (right).
// RVB: dispersion, twelve first-order allpasses (coefficient down to
// -0.9: the lows come up to ~4.5 ms after the highs on every pass), so the
// repeats smear into falling chirps and washes.
// Then a clip at full scale (both can raise peaks).
void BarberpoleMode::LoopFx(FloatFrame* stage, size_t n) {
  for (int ch = 0; ch < 2; ++ch) {
    float* x = &stage[0].l + ch;
    float* ap = k_.disp + 12 * ch;
    float m = k_.disp_mix;
    for (size_t i = 0; i < n; ++i, x += 2) {
      float w = *x;
      if (k_.bal != 0.0f) {
        float& tone = k_.tone[ch];
        tone += k_.tone_coef * (w - tone);
        w = k_.bal < 0.0f ? tone : w - tone;
      }
      m += k_.disp_inc;
      if (m > 0.0f) {
        float v = w;
        for (int k = 0; k < 12; ++k) {
          const float u = v - k_.disp_coef * ap[k];
          v = k_.disp_coef * u + ap[k];
          ap[k] = u;
        }
        w += m * (v - w);
      }
      *x = w > 1.0f ? 1.0f : (w < -1.0f ? -1.0f : w);
    }
  }
  k_.disp_mix += k_.disp_inc * static_cast<float>(n);
}

}  // namespace monsoon
