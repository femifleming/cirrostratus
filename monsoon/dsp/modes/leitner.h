// Mode 17: LEITNER - spatial path panner (after Leitner / Xenakis).
//
// The input (summed to mono) becomes a sound source flying along a path
// around the listener. Rendered for headphones and speakers alike with:
// interaural time difference (<= 0.66 ms), level difference and head-shadow
// low-pass on the far ear, rear darkening, distance gain, air absorption,
// Doppler (propagation delay, <= 30 ms) and 4 early reflections from a
// virtual square room (image sources, each panned to its own direction).
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | Path centre azimuth: noon = front, 0.75 = left, 0.25 =      |
// |          | right, ends = behind.                                       |
// | SIZE     | Path radius / distance, 1 m -> 10 m.                        |
// | DENSITY  | Speed 0.02 Hz -> 4 Hz (one trip round the path per cycle).  |
// | TEXTURE  | Room: 0 open field (no reflections) -> 1 big room (walls    |
// |          | 12 m away, strong reflections).                             |
// | PITCH    | Doppler strength: CCW none, noon physical, CW x3.           |
// | FREEZE   | Stop moving (the source stays where it is).                 |
// | TRIG     | Next path: circle -> figure-8 -> fly-by line -> spiral ->   |
// |          | random flight -> circle ...                                 |
// | Options  | none                                                        |
//
// Per-mode knobs:
//   BAL  noon = v1.0 (a smooth glide along the path). Left, after Leitner:
//        the path becomes 8 loudspeaker stations; the source rests at each
//        and hops to the next (all the way: hops take 15 % of the time).
//        Right, after Xenakis: chance; each trip round the path takes a
//        length drawn from an exponential law (mean = DENSITY's), faded
//        in from regular trips at noon.
//   FBK  trail: the sound is re-emitted every 150 ms (darker each time,
//        up to 0.85 feedback) from wherever the source has moved to, so
//        echoes are left along the path.
//   RVB  the engine's FDN reverb: the room's late tail round the path (the
//        Leitner plug-in's one room for the whole sphere).
//
// Memory: one 8192-sample float delay line (32 KB).

#ifndef MONSOON_DSP_MODES_LEITNER_H_
#define MONSOON_DSP_MODES_LEITNER_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class LeitnerMode : public Mode {
 public:
  static const size_t kLineSize = 8192;
  static const int kPaths = 5;
  static const int kReflections = 4;
  // Ramped per-sample parameters: 2 ear delays, 2 ear gains, then
  // delay / left gain / right gain for each reflection.
  static const int kParams = 4 + 3 * kReflections;

  const char* name() const { return "leitner"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }
  float reverb_amount(const Controls& c) const { return c.rvb; }

  int path() const { return path_; }
  // Source position (metres, x forward, y left) after the last block.
  float source_x() const { return x_; }
  float source_y() const { return y_; }
  float phase() const { return phase_; }

 private:
  void PathPoint(float phase, float radius, float centre, float* x, float* y);
  inline float Tap(float delay) const;

  float* line_;
  size_t write_;
  float phase_;
  int path_;
  float x_, y_;
  float flight_from_[2], flight_to_[2];
  float value_[kParams];
  float lp_coef_[2], lp_[2];
  float refl_lp_[2];
  Random rng_;
  bool frozen_;
  float trip_;       // BAL right: this trip's length (x the mean)
  float trail_gain_, trail_lp_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_LEITNER_H_
