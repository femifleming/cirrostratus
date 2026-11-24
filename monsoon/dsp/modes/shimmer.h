// Mode 11: SHIMMER - shimmer cascade.
//
// A 4-line diffuse delay network (Hadamard feedback, 16-bit lines) with
// three pitch shifters inside the loop, so every pass through the network
// climbs (or falls) by the chosen harmonic set.

#ifndef MONSOON_DSP_MODES_SHIMMER_H_
#define MONSOON_DSP_MODES_SHIMMER_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

// Two-tap crossfading delay-line pitch shifter with Hermite reads.
class ShimmerShifter {
 public:
  static const size_t kLength = 2048;
  static const float kWindow;  // sweep length in samples

  void Init(float* buffer) {
    buffer_ = buffer;
    write_ = 0;
    phase_ = 0.0f;
  }
  void Clear() { memset(buffer_, 0, kLength * sizeof(float)); }
  inline float Process(float x, float ratio) {
    buffer_[write_] = x;
    phase_ += (1.0f - ratio) / kWindow;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    if (phase_ < 0.0f) phase_ += 1.0f;
    float p2 = phase_ + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    float a = Tap(phase_ * kWindow + 2.0f);
    float b = Tap(p2 * kWindow + 2.0f);
    // sin^2 / cos^2 windows sum to one: the shifter never adds gain, so
    // the loop stays stable (FREEZE compensates the small energy loss).
    float wa = FastSin(phase_ * 0.5f);
    float wb = FastSin(p2 * 0.5f);
    float y = a * wa * wa + b * wb * wb;
    if (++write_ >= kLength) write_ = 0;
    return y;
  }

 private:
  inline float At(int32_t i) const {
    return buffer_[static_cast<size_t>(i) & (kLength - 1)];
  }
  inline float Tap(float delay) const {
    float pos = static_cast<float>(write_) - delay + kLength;
    int32_t i = static_cast<int32_t>(pos);
    float t = pos - i;
    return Hermite(At(i - 1), At(i), At(i + 1), At(i + 2), t);
  }
  float* buffer_;
  size_t write_;
  float phase_;
};

class ShimmerMode : public Mode {
 public:
  static const int kLines = 4;
  static const int kShifters = 3;

  const char* name() const { return "shimmer"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

 private:
  void Clear();
  inline float ReadLine(int i, float delay) const;
  inline float Diffuse(int i, float x);

  int16_t* line_[kLines];
  size_t length_[kLines];
  size_t write_[kLines];
  float lp_[kLines];
  float size_smoothed_;
  ShimmerShifter shifter_[kShifters];
  float shift_mix_[kShifters];
  float* ap_buffer_[4];
  size_t ap_pos_[4];
  DcBlocker dc_[2];
  float loop_power_;
  float hold_power_;
  float hold_gain_;
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SHIMMER_H_
