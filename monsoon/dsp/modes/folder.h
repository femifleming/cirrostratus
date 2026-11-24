// Mode 25 - FOLDER: west-coast wavefolder into a vactrol low-pass gate.
//
// Controls
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | fold amount: 1x (soft saturation) -> 10x (many folds)        |
// | SIZE     | symmetry: bias into the folder; noon = symmetric (odd        |
// |          | harmonics), either side adds even harmonics                  |
// | DENSITY  | LPG mode: 0 = low-pass only, 0.5 = combo (low-pass + VCA),   |
// |          | 1 = VCA only                                                 |
// | TEXTURE  | vactrol decay, 30 ms -> 3 s (slower as it closes, like a     |
// |          | real vactrol)                                                |
// | PITCH    | internal sine, V/oct (C3 = 130.81 Hz at noon)                |
// | FREEZE   | LPG held open: a drone                                       |
// | TRIG     | ping the LPG (opens fully, then decays)                      |
// | input    | folded together with the sine; its level also opens the LPG |
// | option 0 | input + internal sine                                        |
// | option 1 | input only (the folder as an effect)                         |
//
// The folder is a sine folder y = sin(pi/2 (g x + bias)), run at 96 kHz:
// half-band interpolation in, half-band decimation out (47-tap Blackman,
// ~74 dB stopband), so the folds do not alias back into the audio band.

#ifndef MONSOON_DSP_MODES_FOLDER_H_
#define MONSOON_DSP_MODES_FOLDER_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class FolderMode : public Mode {
 public:
  static const size_t kRing = 64;

  const char* name() const { return "folder"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  uint8_t num_options() const { return 2; }
  bool frozen() const { return frozen_; }

  // Test hook: vactrol state 0 (closed) .. 1 (open).
  float vactrol() const { return vactrol_; }

 private:
  float Fold(float x) const;

  float* up_[2];     // 1x input history (interpolator)
  float* down_[2];   // 2x folded history (decimator)
  size_t up_w_, down_w_;
  float halfband_[12];
  Svf lpg_[2];
  float phase_;
  float gain_, bias_;
  float vactrol_;
  float lpg_gain_;
  bool frozen_;
  Envelope env_;
  DcBlocker dc_[2];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_FOLDER_H_
