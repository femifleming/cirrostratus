// SPECTRAL option 2 — SMEAR: the spectral smear of Sleepcousonique's Wingie
// mode, ported so it sounds the same (the photograph left out in the rain:
// the spectrum blurred in time and a little across frequency, its partials
// carried on by a phase vocoder; blur from none to seconds).
//
// STFT 512 / hop 128 (4x overlap), Hann analysis and synthesis windows,
// mono like the Wingie's: the mid (L+R)/2 is smeared and sent to both
// outputs (RVB gives it its space). Stereo would be twice the work, more
// than the module has to spare. Per bin the magnitude is smoothed by a
// one-pole whose time is 15 ms x 2^(8.5 blur) (blur 0 passes the sound
// through untouched, 1 is ~5 s) and leans towards its neighbours by
// 0.3 x blur, as on the Wingie.
//
// 1.5.2: the resynthesis is phase-locked. Until 1.5.1 every bin ran on from
// its own smoothed phase deviation, like the Wingie's: under a long blur the
// deviations of a busy input average out to ~0, so every bin sat on its own
// centre frequency, all multiples of fs / N, and the output became a loop of
// one FFT length (10.7 ms at 512): a stutter. And the 3-4 bins of one
// partial drifted against each other and cancelled now and then: dropouts.
// Now the phase deviation is averaged as a phasor weighted by the magnitude
// (no wrap-around bias, the loudest partial in the bin decides its
// frequency); each peak of the smoothed spectrum runs on at its bin's
// frequency (a random phase drift was tried: it only added flutter); and the
// bins around a peak, up to half-way to the next, take its phase with
// the Hann lobe's alternating sign, so they sound as one partial and can't
// cancel each other.
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
  static const size_t kNativeSize = 512;

  // N = 512 unless the FFT setting says otherwise, hop N/4.
  bool Init(Arena* arena, size_t size = kNativeSize) __attribute__((cold));
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();
  bool frozen() const { return hold_; }

 private:
  void Bins(float* x);

  // Run-time (the FFT setting), named as the constants they were.
  size_t kSize, kHop;
  size_t kRing;   // N + two hops of slack for Prepare
  size_t kBins;   // 1 .. kBins-1 carry sound

  float* in_;
  float* out_;
  float* mag_;
  float* ur_;      // the phase deviation, as a phasor weighted by magnitude
  float* ui_;
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
