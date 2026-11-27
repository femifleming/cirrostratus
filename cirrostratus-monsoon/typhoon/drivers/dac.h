// Typhoon: DAC1 on PA4 drives the AUX normal. U14A (gain 1.5) turns
// 0-3.3 V into 0-~5 V on the AUX jack's switch contact; from there a buffer
// feeds the normalling contact of all nine CV jacks (POS, DENS, SIZE, TEXT,
// FREQ, WET, STEREO, FDBK, VERB; not V/OCT). With nothing in AUX, whatever
// the DAC puts out reaches every unpatched CV input.
//
// Off (the default): PA4 is a push-pull output driven low, so the AUX
// normal is 0 V, as on the stock firmware with its noise off. On: the DAC
// takes software writes (no timer, no wave generator).
//
// Pin and circuit from the Typhoon netlist; the stock firmware's driver
// (SuperParasites, MIT) does the same with its noise generator.

#ifndef TYPHOON_DRIVERS_DAC_H_
#define TYPHOON_DRIVERS_DAC_H_

#include "stmlib/stmlib.h"

namespace monsoon {

class Dac {
 public:
  Dac() { }
  ~Dac() { }

  void Init();             // starts off (0 V)
  void set_enabled(bool on);
  bool enabled() const { return enabled_; }
  void Write(uint16_t code);  // 12-bit, 0..4095; ignored while off

 private:
  bool enabled_;

  DISALLOW_COPY_AND_ASSIGN(Dac);
};

}  // namespace monsoon

#endif  // TYPHOON_DRIVERS_DAC_H_
