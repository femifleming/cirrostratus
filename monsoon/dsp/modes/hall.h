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
// Per-mode knobs:
//   BAL  noon = v1.0. Bass decay (below ~250 Hz) relative to DENSITY's
//        decay: left lows die sooner (x0.25 at full: a clear, airy
//        hall), right lows ring longer (x3: a warm, heavy one).
//   FBK  pre-delay echoes: the pre-delay repeats (time = POSITION, up to
//        0.75 regeneration), and every echo blooms into the hall.
//        Fades in over the first 50 ms of POSITION (no echoes at 0).
//   RVB  diffusion: 0 = v1.0's smooth wash; turning it up backs the
//        diffusers off (input and in-line allpasses, 0.65 / 0.55 -> 0),
//        so the hall turns grainy and its echoes come apart: a stone
//        stairwell, a flutter of slaps on every click.
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
  inline float LineAllpass(int i, float x, float g);
  inline float InputAllpass(int i, float x, float g);
  // Per-mode knob helpers, out of line (they cost nothing when off).
  __attribute__((noinline, cold)) void Bass(float* y, const float* extra);
  __attribute__((noinline, cold)) void BassGains(
      float t60, const float* gain, float* extra) const;

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
  float lo_[kLines];     // BAL: bass part of each line (one-pole)
  float echo_;           // FBK: last pre-delay output
  float scale_;
  float pre_delay_;
  float lfo_phase_;
  DcBlocker dc_[2];
  volatile bool clear_pending_;
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_HALL_H_
