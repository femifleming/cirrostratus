// Mode 6 — CROSS: L/R spectral morph (left = modulator, right = carrier).
//
// STFT 2048 / hop 512. See DESIGN.md.

#ifndef MONSOON_DSP_MODES_CROSS_H_
#define MONSOON_DSP_MODES_CROSS_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/stft.h"

namespace monsoon {

class CrossMode : public Mode, public StftClient {
 public:
  static const size_t kFftSize = 2048;
  static const size_t kHop = 512;
  static const size_t kBins = kFftSize / 2;

  const char* name() const { return "cross"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { stft_.Prepare(); }
  bool frozen() const { return ctl_.freeze; }

  // StftClient.
  void Transform(float* spectrum[2], size_t fft_size);

  bool carrier_held() const { return hold_carrier_; }

 private:
  // Constant-Q box smoothing across bins (prefix sums in `scratch`).
  void SmoothBins(const float* in, float* out, float q);

  Stft stft_;
  float* env_l_;       // time-smoothed modulator magnitudes (held on freeze)
  float* env_r_;       // time-smoothed carrier envelope (for whitening)
  float* car_mag_;     // carrier magnitudes (held on TRIG)
  float* car_freq_;    // carrier instantaneous frequency, bins
  float* car_phase_;   // carrier analysis phase
  float* syn_phase_;   // output synthesis phase
  float* out_mag_;     // transposed carrier
  float* out_freq_;
  float* smooth_l_;
  float* smooth_r_;
  float* prefix_;      // kBins + 1

  Controls ctl_;
  volatile bool hold_carrier_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CROSS_H_
