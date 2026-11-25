// SPECTRAL option 2 — SMEAR: the spectral smear of Sleepcousonique's Wingie
// mode, ported so it sounds the same (the photograph left out in the rain:
// the spectrum blurred in time and a little across frequency, its partials
// carried on by a phase vocoder; blur from none to seconds).
//
// STFT 512 / hop 128 (4x overlap), Hann analysis and synthesis windows,
// mono like the Wingie's: the mid (L+R)/2 is smeared and sent to both
// outputs (RVB gives it its space). Stereo would be twice the work, more
// than the module has to spare. Per bin: the magnitude and the phase deviation are each smoothed
// by a one-pole whose time is 15 ms x 2^(8.5 blur) (blur 0 passes the sound
// through untouched, 1 is ~5 s); the magnitude also leans towards its
// neighbours by 0.3 x blur; the phase runs on from the smoothed deviation.
// The same sine and atan2 approximations as the Wingie, so the beating of
// a long blur (part of its sound) is the same.
//
// Controls: DENSITY blur. FREEZE holds the spectrum as it is (the blur
// stops moving). The engine's WET and RVB (the FDN reverb) as usual.
//
// The audio interrupt only moves samples; the transforms run in Prepare()
// (main loop), like SPECTRAL's own STFT; a frame is heard two hops after it
// is due, so the main loop has 5 ms for it. Latency N + 2 hops = 768
// samples (16 ms).

#ifndef MONSOON_DSP_MODES_SMEAR_H_
#define MONSOON_DSP_MODES_SMEAR_H_

#include "monsoon/dsp/cmsis/cmsis_fft.h"
#include "monsoon/dsp/core.h"

namespace monsoon {

class SmearEngine {
 public:
  static const size_t kSize = 512;
  static const size_t kHop = 128;
  static const size_t kRing = kSize + 2 * kHop;   // two hops of slack for Prepare
  static const size_t kBins = kSize / 2;   // 1 .. kBins-1 carry sound

  bool Init(Arena* arena) __attribute__((cold));
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();
  bool frozen() const { return hold_; }

 private:
  void Bins(float* x);

  float* in_;
  float* out_;
  float* mag_;
  float* dev_;
  float* prev_;
  float* acc_;
  float* win_;
  float* temp_;
  float* spec_;
  size_t pos_;
  size_t hop_counter_;
  volatile uint32_t hops_ready_;
  uint32_t hops_done_;
  volatile float blur_;
  volatile bool hold_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SMEAR_H_
