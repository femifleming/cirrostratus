// Mode 13 - PLATES: sympathetic plates (modal synthesis, after the Ronin
// plates of the Oneirophone).
//
// A rectangular plate's bending modes sit at f(m,n) proportional to
// m^2 + (a*n)^2 (a = aspect ratio). SIZE sweeps the aspect ratio from a
// square (rich, clustered, near-degenerate pairs) to a long bar (beam-like
// m^2 series), the 24 lowest modes are kept, and each becomes a two-pole
// resonator. POSITION moves the strike point (mode amplitude =
// sin(m*pi*x) sin(n*pi*y)); two fixed pickups give the stereo image.

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
    uint8_t m, n;
  };

  void SelectModes(float aspect);
  void UpdateCoefficients(const Controls& c);

  Resonator* modes_;
  float cache_[6];
  bool first_block_;
  bool frozen_;
  DcBlocker dc_[2];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_PLATES_H_
