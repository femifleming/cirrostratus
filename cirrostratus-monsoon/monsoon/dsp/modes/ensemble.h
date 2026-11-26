// Mode 21: ENSEMBLE - string-machine ensemble chorus.
//
// Three modulated delay taps per side, driven by a slow (~0.6 Hz) and a
// fast (~6 Hz) LFO at 120-degree phase steps (the right side runs 60
// degrees further on), like the Solina/ARP string ensembles.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | Character: 0 chorus (one slow tap + dry) -> 0.5 ensemble    |
// |          | (three taps, slow + fast LFOs, some dry) -> 1 vibrato       |
// |          | (one fast tap, no dry).                                     |
// | SIZE     | Base delay 5 -> 15 ms.                                      |
// | DENSITY  | Modulation depth (0 = static delays).                       |
// | TEXTURE  | 0 clean hi-fi -> 1 dark bucket-brigade (low-pass 20 kHz ->  |
// |          | 3.5 kHz, gentle saturation).                                |
// | PITCH    | LFO rate, V/oct (noon = 0.6 Hz slow / 6 Hz fast).           |
// | FREEZE   | Stop the LFOs where they are.                               |
// | TRIG     | Reset the LFO phases.                                       |
// | Options  | none                                                        |
//
// Per-mode knobs:
//   BAL  slow vs fast LFO, noon = v1.0. Left: the fast LFO fades out and
//        the slow one deepens (x1.5): a pure swirl. Right: the slow LFO
//        fades out and the fast one deepens (+0.6 ms, even in chorus):
//        pure shimmer.
//   FBK  regeneration: the taps are fed back into the delay lines (loop
//        gain up to 0.85, high-passed at 100 Hz, through the bucket-
//        brigade filter): flanger jets and metallic ensembles.
//   RVB  the engine's FDN reverb (the string machine's hall).
//
// Memory: 2 x 2048 float delay lines (16 KB).

#ifndef MONSOON_DSP_MODES_ENSEMBLE_H_
#define MONSOON_DSP_MODES_ENSEMBLE_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/knob_fx.h"

namespace monsoon {

class EnsembleMode : public Mode {
 public:
  static const size_t kLineSize = 2048;  // 42 ms, power of two
  static const int kTaps = 3;

  const char* name() const { return "ensemble"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }
  float reverb_amount(const Controls& c) const { return c.rvb; }

 private:
  inline float Tap(int ch, float delay) const;
  void SetBbd(float f) KNOB_FX;
  bool Knobs(const Controls& c, const float* w, size_t n, float* slow_share,
             float* fast_ms) KNOB_FX;
  void Regenerate(int ch, const FloatFrame* in, const FloatFrame* out,
                  size_t n, float dry, float wet) KNOB_FX;

  float* line_[2];
  size_t write_;
  float slow_phase_, fast_phase_;
  float base_delay_;
  Svf bbd_[2][2];
  float regen_, regen_inc_;     // FBK
  float regen_lp_[2];
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_ENSEMBLE_H_
