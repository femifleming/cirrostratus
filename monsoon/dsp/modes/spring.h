// Mode 1 - SPRING: a dispersive spring reverb tank (1.5.2; mode 1 was
// GRAINS until 1.5).
//
// A spring carries low frequencies slower than high ones, so every echo
// arrives as a falling chirp: the "boing" and the "drip" of a guitar amp's
// tank. Each spring here is a delay loop (one trip along the spring and
// back) with a cascade of 32 stretched first-order allpasses inside it
// (H(z) = (a + z^-4) / (1 + a z^-4), after Valimaki, Parker and Abel,
// "Parametric spring reverberation effect", JAES 2010): every pass through
// the loop disperses the echo further, as on a real spring.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | tone: the tank's band limit, dark 1.2 kHz -> bright 8 kHz    |
// | SIZE     | spring length: 25 ms -> 80 ms per trip                       |
// | DENSITY  | decay: 0.4 s -> 8 s                                          |
// | TEXTURE  | dispersion: a = -0.2 (a short splash, ~2 ms of chirp per     |
// |          | trip) -> -0.75 (long, liquid chirps, ~18 ms: the drip)       |
// | PITCH    | spring tension: the trip time follows V/oct down to 5 ms     |
// |          | and up to 150 ms (the flutter between echoes is pitched)     |
// | FREEZE   | the tank rings on: input off, no decay, tone open            |
// | TRIG     | kick the tank: a knock into every spring (the crash)         |
// | option 0 | a pair: spring A left, B right                               |
// | option 1 | a tank of three: A left, B right, C in the middle            |
// | option 2 | one spring, to both sides (BAL has nothing to widen)         |
//
// Per-mode knobs:
//   BAL  stereo width: left = the springs summed (mono), noon = each on
//        its side, right = wider still (side x2).
//   FBK  the engine's feedback loop: the tank's output back into its
//        input (dub).
//   RVB  dwell: how hard the tank is driven, 0 = clean, up to x6 into the
//        driver's saturation (a hot spring: grit and bloom).
//
// CPU: 32 allpasses per spring, 2 FLOPs each (~1 k instructions per frame
// with three springs). Memory: 3 x 150 ms float lines, 86 KB.

#ifndef MONSOON_DSP_MODES_SPRING_H_
#define MONSOON_DSP_MODES_SPRING_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class SpringMode : public Mode {
 public:
  static const int kSprings = 3;
  static const int kStages = 32;     // allpasses per spring
  static const int kStretch = 4;     // z^-4: dispersion below fs / 8
  static const size_t kLineSize = 7200;   // 150 ms

  const char* name() const { return "spring"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  uint8_t num_options() const { return 3; }
  bool frozen() const { return frozen_; }
  float feedback_amount(const Controls& c) const { return c.fbk; }

  // Test hooks.
  float trip_samples(int s) const { return trip_[s]; }
  float dispersion() const { return a_; }

 private:
  struct Spring {
    float* line;
    size_t write;
    float v[kStages][kStretch];   // each allpass's last kStretch states
    float lp1, lp2;               // the band limit (two one-poles)
    float hp;                     // the loop's DC / rumble guard
  };
  inline float Run(Spring* s, float x, float trip, float g, float lp_coef,
                   float a, int k);

  Spring spring_[kSprings];
  float trip_[kSprings];          // smoothed trip times, samples
  float a_;                       // smoothed dispersion
  float in_hp_;                   // input high-pass state
  float knock_;                   // TRIG: the knock's envelope
  uint32_t rng_;
  int k_;                         // shared index into v[][]
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SPRING_H_
