// Mode 23 - SPECDELAY: spectral delay.
//
// The mid signal is analysed (FFT 512, hop 256, sine windows); every bin
// has its own delay, in frames of 5.3 ms, and its own feedback. The delay
// memory stores int16 complex bins with a per-frame scale (~0.5 s max).
// Left and right read the same memory, the right side one frame later on
// odd bins, which makes the echoes wide.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | delay curve: 0 lows come back last, 0.5 all bins together,  |
// |          | 1 highs come back last (log-frequency ramp)                 |
// | SIZE     | longest delay, 5 ms -> ~0.5 s                               |
// | DENSITY  | feedback 0 -> 0.98                                          |
// | TEXTURE  | feedback tilt: 0 highs die first (dark repeats), 0.5 flat,  |
// |          | 1 lows die first (thin, bright repeats)                     |
// | PITCH    | spectral shift per feedback pass (ratio 2^(pitch/12), +/-24 |
// |          | st): every repeat moves up/down -> spirals; noon = none     |
// | FREEZE   | input muted, feedback 1: the spectral echoes hold forever   |
// | TRIG     | clear the delay memory                                      |
// | options  | none                                                        |
//
// Per-mode knobs:
//   BAL  delay pattern on top of the POSITION curve, noon = v1.0. Left:
//        stripes (the delay zig-zags four times across the spectrum, so
//        neighbouring bands answer early and late); right: scatter (every
//        bin gets its own random delay, up to the whole SIZE).
//   FBK  spectral blur: each written frame has its magnitudes averaged
//        with the neighbouring bins (up to 60 %), so every repeat smears
//        further across frequency: tones widen into bands of hiss.
//   RVB  phase diffusion: a share of each written frame's bins (all at
//        full) is turned by a random multiple of 90 degrees, so every
//        repeat smears further in time: echoes melt into a wash.

#ifndef MONSOON_DSP_MODES_SPECDELAY_H_
#define MONSOON_DSP_MODES_SPECDELAY_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/knob_fx.h"
#include "monsoon/dsp/modes/mono_stft.h"

namespace monsoon {

class SpecDelayMode : public Mode, public MonoStftClient {
 public:
  static const size_t kFftSize = 512;
  static const size_t kHop = 256;
  static const size_t kBins = kFftSize / 2;  // slots per frame (1..255 used)
  static const size_t kMaxFrames = 96;

  const char* name() const { return "specdelay"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { stft_.Prepare(); }
  bool frozen() const { return ctl_.freeze; }

  void Transform(float* in, float* out[2], size_t fft_size);
  size_t frames() const { return frames_; }

 private:
  inline void Read(size_t frame, size_t k, float* re, float* im) const;
  float Pattern(size_t k, float curve, float bal) const KNOB_FX;
  float Smear(float* re, float* im, float fbk, float rvb) KNOB_FX;

  MonoStft stft_;
  int16_t* memory_;   // frames_ x kBins x (re, im)
  float* scale_;      // per frame
  float* log_pos_;    // log2(k) / log2(255) per bin
  size_t frames_;
  size_t write_;
  Controls ctl_;
  Random rng_;
  volatile bool clear_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SPECDELAY_H_
