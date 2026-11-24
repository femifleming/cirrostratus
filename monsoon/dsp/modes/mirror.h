// Mode 29 - MIRROR: spectral warping.
//
// The mid signal is analysed (FFT 1024, hop 256, sine windows). Every
// partial (peak region of the spectrum) is moved as one block to a new
// frequency f' = warp(f), resynthesised with running phases (phase
// vocoder with rigid phase locking). Output is mono on both sides (STER
// widens it).
//
// Warps, relative to the anchor A:
//   stretch  f' = A (f/A)^1.6    partials spread apart: inharmonic, bell-like
//   invert   f' = A^2 / f        log-frequency mirror: a fifth above A
//                                 becomes a fifth below, highs become lows
//   fold     f' = A tri(f/A)     everything above A folds back under it
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | warp type: 0 stretch -> 0.5 invert -> 1 fold (crossfaded)   |
// | SIZE     | amount: 0 no warp (clean pass-through) -> 1 full warp       |
// | PITCH    | anchor / mirror frequency, V/oct, C4 = 261.6 Hz at noon     |
// | DENSITY  | spectral blur (magnitudes smeared over time)                |
// | TEXTURE  | tilt -9 ... +9 dB/oct around 1 kHz, flat at noon            |
// | FREEZE   | hold the spectrum (the warp stays live on the held sound)   |
// | TRIG     | grab a new spectrum while frozen                            |
// | options  | none                                                        |

#ifndef MONSOON_DSP_MODES_MIRROR_H_
#define MONSOON_DSP_MODES_MIRROR_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/mono_stft.h"

namespace monsoon {

class MirrorMode : public Mode, public MonoStftClient {
 public:
  static const size_t kFftSize = 1024;
  static const size_t kHop = 256;
  static const size_t kBins = kFftSize / 2;

  const char* name() const { return "mirror"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { stft_.Prepare(); }
  bool frozen() const { return ctl_.freeze; }

  void Transform(float* in, float* out[2], size_t fft_size);

  // The warp (in bins), exposed for tests.
  static float Warp(float f, float anchor, float position, float amount);

 private:
  MonoStft stft_;
  float* mag_;        // smoothed / frozen magnitudes
  float* freq_;       // instantaneous frequency (bins)
  float* an_phase_;   // previous analysis phase (cycles)
  float* syn_phase_;  // running synthesis phase (cycles), per output bin
  float* out_mag_;    // summed magnitude per output bin
  float* best_;       // strongest contribution so far
  float* pfreq_;      // warped frequency of the peak owning the bin
  int16_t* off_;      // bin offset from its peak (0 = the peak itself)
  Controls ctl_;
  volatile bool grab_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_MIRROR_H_
