// Mode 28 - GAMELAN: seven metallophone keys in pelog or slendro.
//
// Controls
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | which key TRIG strikes (7 zones, low -> high)                |
// | SIZE     | ombak: beating between each key and its twin, 0 -> 10 Hz     |
// | DENSITY  | decay, 0.3 s -> 20 s (bonang x1.5, gong x3)                  |
// | TEXTURE  | instrument: saron (bar) -> bonang (kettle) -> gong; the gong |
// |          | end also drops two octaves                                  |
// | PITCH    | root, V/oct (C4 = 261.63 Hz at noon)                         |
// | FREEZE   | the bank rings forever, input cut                            |
// | TRIG     | strike the key chosen by POSITION                            |
// | input    | excites every key (sympathetic bank)                         |
// | option 0 | pelog  (0 120 258 539 675 785 943 cents)                     |
// | option 1 | slendro (0 231 474 717 955 1200 1431 cents)                  |
//
// Each key is 4 partials x 2 (the key and its ombak twin, tuned
// +ombak Hz apart like a Balinese pengumbang/pengisep pair) = 8 rotating
// complex resonators, 56 in all. The partial ratios morph (in log
// frequency) between a free bar (1, 2.76, 5.40, 8.93), a kettle gong
// (1, 1.48, 2.79, 3.64) and a hanging gong (1, 1.44, 2.00, 2.99).

#ifndef MONSOON_DSP_MODES_GAMELAN_H_
#define MONSOON_DSP_MODES_GAMELAN_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class GamelanMode : public Mode {
 public:
  static const int kKeys = 7;
  static const int kPartials = 4;
  static const int kResonators = kKeys * kPartials * 2;

  const char* name() const { return "gamelan"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  uint8_t num_options() const { return 2; }
  bool frozen() const { return frozen_; }

  // Test hooks.
  float key_hz(int key) const { return key_hz_[key]; }
  void Strike(int key, float amp);

 private:
  struct Resonator {
    float cr, ci, re, im;
    float in_gain;     // input drive (constant peak gain)
    float amp;         // partial amplitude (strike + output weight)
    float wl, wr;      // output weights
  };

  void Update(const Controls& c);

  Resonator* res_;
  float key_hz_[kKeys];
  float cache_[6];
  bool first_;
  bool frozen_;
  DcBlocker dc_[2];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_GAMELAN_H_
