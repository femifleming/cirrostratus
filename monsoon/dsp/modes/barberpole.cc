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
  if (!loop_.Init(arena, 98 * 1024, 2)) return false;
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
  float target = kMinDelay * powf(kMaxDelay / kMinDelay, Clamp(c.size, 0.0f, 1.0f));
  float d_start = delay_;
  delay_ += 0.1f * (target - delay_);
  if (delay_ > kMaxDelay) delay_ = kMaxDelay;
  if (delay_ < kMinDelay) delay_ = kMinDelay;
  const float d_inc = (delay_ - d_start) / static_cast<float>(n);

  float fb_start = feedback_;
  feedback_ = frozen_ ? 1.0f : Clamp(c.density, 0.0f, 1.0f) * 0.98f;
  const float fb_inc = (feedback_ - fb_start) / static_cast<float>(n);
  const float in_target = frozen_ ? 0.0f : 1.0f;

  FloatFrame stage[kBlockSize];
  const float head = static_cast<float>(loop_.write_head());
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
  for (size_t i = n; i < kBlockSize; ++i) stage[i].l = stage[i].r = 0.0f;
  loop_.WriteBlock(stage);
}

}  // namespace monsoon
