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
// Memory: 2 x 2048 float delay lines (16 KB).

#ifndef MONSOON_DSP_MODES_ENSEMBLE_H_
#define MONSOON_DSP_MODES_ENSEMBLE_H_

#include "monsoon/dsp/mode.h"

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

 private:
  inline float Tap(int ch, float delay) const;

  float* line_[2];
  size_t write_;
  float slow_phase_, fast_phase_;
  float base_delay_;
  Svf bbd_[2][2];
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_ENSEMBLE_H_
