// Mode 6 — CROSS: L/R spectral morph (left = modulator, right = carrier).
//
// STFT 2048 / hop 1024 (sine windows), mono synthesis. See DESIGN.md.

#ifndef MONSOON_DSP_MODES_CROSS_H_
#define MONSOON_DSP_MODES_CROSS_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/lean_stft.h"

namespace monsoon {

class CrossMode : public Mode, public LeanStftClient {
 public:
  static const size_t kFftSize = LeanStft::kSize;
  static const size_t kHop = LeanStft::kHop;
  static const size_t kBins = kFftSize / 2;

  const char* name() const { return "cross"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { stft_.Prepare(); }
  bool frozen() const { return ctl_.freeze; }

  // LeanStftClient.
  void TransformJoint(float* left, float* right);

  bool carrier_held() const { return hold_carrier_; }

 private:
  // Constant-Q box smoothing across bins (prefix sums in `prefix_`).
  void SmoothBins(const float* in, float* out, float q);

  LeanStft stft_;
  float* env_l_;       // time-smoothed modulator magnitudes (held on freeze)
  float* env_r_;       // time-smoothed carrier envelope (for whitening)
  float* car_mag_;     // carrier magnitudes (held on TRIG)
  float* car_freq_;    // carrier instantaneous frequency, bins (peaks)
  float* car_prev_;    // previous carrier spectrum (CMSIS order)
  float* syn_phase_;   // output synthesis phase
  float* out_mag_;     // transposed carrier
  float* out_freq_;
  float* smooth_l_;
  float* smooth_r_;
  float* prefix_;      // kBins + 1; reused for the output magnitudes

  Controls ctl_;
  volatile bool hold_carrier_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CROSS_H_
