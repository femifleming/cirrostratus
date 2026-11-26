// Mode 13 - PLATES: sympathetic plates (modal synthesis, after the Ronin
// plates of the Oneirophone).
//
// A rectangular plate's bending modes sit at f(m,n) proportional to
// m^2 + (a*n)^2 (a = aspect ratio). SIZE sweeps the aspect ratio from a
// square (rich, clustered, near-degenerate pairs) to a long bar (beam-like
// m^2 series), the 24 lowest modes are kept, and each becomes a two-pole
// resonator. POSITION moves the strike point (mode amplitude =
// sin(m*pi*x) sin(n*pi*y)); two fixed pickups give the stereo image.
//
// Per-mode knobs:
//   BAL  noon = v1.0. Left: the modes are pulled onto whole-number
//        harmonics of the root (fully left = a tuned, pitched plate).
//        Right: the series is stretched (ratio^1.6 at full), more
//        clangorous, bell / tam-tam like.
//   FBK  tension: a loud plate rings sharp and glides down as it decays
//        (amplitude-dependent pitch, up to ~+3 semitones on hard hits).
//   RVB  the engine's FDN reverb (a struck plate is otherwise dry).

#ifndef MONSOON_DSP_MODES_PLATES_H_
#define MONSOON_DSP_MODES_PLATES_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class PlatesMode : public Mode {
 public:
  static const int kNumModes = 24;

  const char* name() const { return "plates"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }
  float reverb_amount(const Controls& c) const { return c.rvb; }

  // Test hooks: frequency (Hz) of mode i after the last block.
  float mode_frequency(int i) const { return modes_[i].hz; }

 private:
  struct Resonator {
    float a1, a2, b;     // y = b*x + a1*y1 - a2*y2
    float strike;        // excitation weight at the strike point
    float pl, pr;        // pickup weights
    float y1, y2;
    float hz;
    float sin_w;
    float cos_w, sin_b;  // FBK: cos / sin of the (bent) frequency
    float two_r;         // a1 = two_r * cos_w
    uint8_t m, n;
  };

  void SelectModes(float aspect);
  void UpdateCoefficients(const Controls& c);

  Resonator* modes_;
  void ApplyTension(float bend, bool rescale);

  float cache_[7];
  float bal_used_;   // BAL's target, past the knob's noise
  float selected_size_;  // the SIZE the plate's modes were chosen for
  float energy_;     // sum of the modes' energy after the last block
  float bend_;       // FBK: relative pitch offset applied to a1
  float bal_;        // BAL, bipolar and smoothed
  bool first_block_;
  bool frozen_;
  DcBlocker dc_[2];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_PLATES_H_
