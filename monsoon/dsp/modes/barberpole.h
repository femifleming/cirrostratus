// Mode 22: BARBERPOLE - frequency-shift spirals.
//
// A single-sideband frequency shifter sits inside a delay feedback loop:
// every trip round the loop shifts the sound further, so repeats climb (or
// fall) endlessly - the audio version of a barber's pole.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | Shift amount, bipolar: noon = 0 Hz, CW up, CCW down,        |
// |          | 0.1 Hz -> 2 kHz (exponential). Small dead zone at noon.     |
// | SIZE     | Loop delay 1.5 ms -> 500 ms (exponential).                  |
// | DENSITY  | Feedback 0 -> 0.98.                                         |
// | TEXTURE  | Up/down mix: 0 both sides shift the same way, 1 = left goes |
// |          | the POSITION way, right the opposite way.                   |
// | PITCH    | Multiplies the shift, V/oct (+12 st = twice the shift).     |
// | FREEZE   | Infinite spiral: input muted, feedback 1.                   |
// | TRIG     | Flip the shift direction.                                   |
// | Options  | none                                                        |
//
// The loop holds 30 Hz high-pass and 14 kHz low-pass filters so shifted
// material leaves the band instead of folding round DC or Nyquist.
//
// Per-mode knobs:
//   BAL  tone of the loop, noon = v1.0 (30 Hz - 14 kHz). Left: a one-pole
//        low-pass closing 14 kHz -> 1.2 kHz (every pass darker); right: a
//        one-pole high-pass opening up to 1.5 kHz (every pass thinner).
//   FBK  loop wow: the loop delay wobbles (a 0.9 Hz triangle, up to
//        +2.5 ms, at most a quarter of the delay), so every pass is
//        detuned and smeared a little more, like tape.
//   RVB  dispersion in the loop: twelve first-order allpasses per side
//        (coefficient down to -0.9: the lows up to ~4.5 ms behind the
//        highs on every pass), so the spiralling repeats smear into
//        falling chirps, like a spring.
//
// Memory: block-floating-point stereo loop (~96 KB, 500 ms).

#ifndef MONSOON_DSP_MODES_BARBERPOLE_H_
#define MONSOON_DSP_MODES_BARBERPOLE_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/knob_fx.h"
#include "monsoon/dsp/sample_buffer.h"

namespace monsoon {

// 90-degree phase splitter (Olli Niemitalo's allpass pair), a private copy
// of the one in DIFFERENCE.
class BarberSplitter {
 public:
  void Init() {
    memset(sa_, 0, sizeof(sa_));
    memset(sb_, 0, sizeof(sb_));
    a_delay_ = 0.0f;
  }
  inline void Process(float x, float* re, float* im) {
    float a = x;
    for (int i = 0; i < 4; ++i) a = Section(&sa_[i], ka_[i], a);
    float b = x;
    for (int i = 0; i < 4; ++i) b = Section(&sb_[i], kb_[i], b);
    *re = a_delay_;
    *im = b;
    a_delay_ = a;
  }

 private:
  struct State { float x1, x2, y1, y2; };
  static inline float Section(State* s, float k, float x) {
    float y = k * (x + s->y2) - s->x2;
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
  }
  static const float ka_[4];
  static const float kb_[4];
  State sa_[4], sb_[4];
  float a_delay_;
};

class BarberpoleMode : public Mode {
 public:
  const char* name() const { return "barberpole"; }
  bool Init(Arena* arena);
  bool uses_buffer_quality() const { return true; }
  float max_delay_seconds() const { return max_delay_ / kSampleRate; }  // tests
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

  // Shift (Hz) applied to the left / right side in the last block.
  float shift_hz(int side) const { return shift_[side]; }

 private:
  float Knobs(const Controls& c, size_t n) KNOB_FX;
  void LoopFx(FloatFrame* stage, size_t n) KNOB_FX;

  SampleBuffer loop_;
  BarberSplitter splitter_[2];
  Svf hp_[2], lp_[2];
  float phase_[2];
  float shift_[2];
  float delay_;
  float min_delay_, max_delay_;
  float feedback_;
  float input_gain_;
  float direction_;
  // BAL / FBK / RVB state (zeroed in Init).
  struct {
    float bal;
    float tone_coef;     // BAL's one-pole in the loop
    float tone[2];
    float wow;           // FBK LFO phase
    float wow_depth;     // samples
    float wow_shape;     // LFO value at the end of the last block, 0..1
    float wow_inc;       // per-sample ramp of the offset in this block
    bool loop_fx;        // LoopFx runs this block
    float disp_coef, disp_mix, disp_inc;   // RVB
    float disp[24];      // 2 sides x 12 first-order allpasses
  } k_;
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_BARBERPOLE_H_
