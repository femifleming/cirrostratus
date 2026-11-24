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

  int path() const { return path_; }
  // Source position (metres, x forward, y left) after the last block.
  float source_x() const { return x_; }
  float source_y() const { return y_; }

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
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_LEITNER_H_
