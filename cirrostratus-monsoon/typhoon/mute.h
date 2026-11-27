// Click-free digital mute for the Typhoon's IN MUTE and OUT MUTE buttons.
// A linear gain ramp of kRampSamples (5 ms at 48 kHz); fully open it leaves
// the samples untouched (bit-transparent), fully closed it writes zeros.

#ifndef TYPHOON_MUTE_H_
#define TYPHOON_MUTE_H_

#include "stmlib/stmlib.h"

#include "typhoon/drivers/codec.h"

namespace monsoon {

class Mute {
 public:
  Mute() { }
  ~Mute() { }

  void Init() {
    muted_ = false;
    gain_ = kUnity;
  }

  // From the UI (1 kHz tick); the audio interrupt follows on its next block.
  void set_muted(bool m) { muted_ = m; }
  void toggle() { muted_ = !muted_; }
  bool muted() const { return muted_; }

  void Process(Codec::Frame* frames, size_t n) {
    const int32_t target = muted_ ? 0 : kUnity;
    if (gain_ == target) {
      if (target == 0) {
        for (size_t i = 0; i < n; ++i) frames[i].l = frames[i].r = 0;
      }
      return;
    }
    const int32_t step = target > gain_ ? kStep : -kStep;
    for (size_t i = 0; i < n; ++i) {
      if (gain_ != target) {
        gain_ += step;
        if ((step > 0 && gain_ > target) || (step < 0 && gain_ < target)) {
          gain_ = target;
        }
      }
      frames[i].l = static_cast<short>(frames[i].l * gain_ >> 16);
      frames[i].r = static_cast<short>(frames[i].r * gain_ >> 16);
    }
  }

 private:
  static const int32_t kUnity = 65536;
  static const int32_t kRampSamples = 240;  // 5 ms
  static const int32_t kStep = kUnity / kRampSamples;

  volatile bool muted_;
  int32_t gain_;

  DISALLOW_COPY_AND_ASSIGN(Mute);
};

}  // namespace monsoon

#endif  // TYPHOON_MUTE_H_
