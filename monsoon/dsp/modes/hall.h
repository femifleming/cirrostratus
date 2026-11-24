// Mode 26: HALL - big algorithmic reverb.
//
// A large 8-line feedback delay network (Hadamard mixing, 16-bit lines up
// to 160 ms each) with an allpass diffuser inside every line, four
// modulated lines, input diffusion and a pre-delay. Separate from (and much
// bigger than) the output-section reverb.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | Pre-delay 0 -> 250 ms.                                      |
// | SIZE     | Room size (line lengths x0.35 -> x1; moving it glides).     |
// | DENSITY  | Decay time 0.4 s -> 30 s (exponential).                     |
// | TEXTURE  | Damping: 0 dark (1.2 kHz) -> 1 bright (16 kHz).             |
// | PITCH    | Modulation: CCW none, noon gentle chorus (~0.5 Hz),         |
// |          | CW deep and faster (~3 Hz).                                 |
// | FREEZE   | Infinite hold: input muted, no decay, no damping.           |
// | TRIG     | Clear the tail.                                             |
// | Options  | none                                                        |
//
// Memory: ~124 KB (lines 88 KB, pre-delay 24 KB, diffusers 12 KB).

#ifndef MONSOON_DSP_MODES_HALL_H_
#define MONSOON_DSP_MODES_HALL_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class HallMode : public Mode {
 public:
  static const int kLines = 8;

  const char* name() const { return "hall"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  // TRIG's 124 KB clear runs here, outside the audio interrupt.
  void Prepare();
  bool frozen() const { return frozen_; }

 private:
  void Clear();
  inline float ReadLine(int i, float delay) const;
  inline float LineAllpass(int i, float x);
  inline float InputAllpass(int i, float x);

  int16_t* line_[kLines];
  size_t line_size_[kLines];
  size_t write_[kLines];
  float* ap_[kLines];
  size_t ap_pos_[kLines];
  float* in_ap_[4];
  size_t in_ap_pos_[4];
  int16_t* pre_;
  size_t pre_pos_;
  float lp_[kLines];
  float scale_;
  float pre_delay_;
  float lfo_phase_;
  DcBlocker dc_[2];
  volatile bool clear_pending_;
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_HALL_H_
