// Copyright 2014 Olivier Gillet.
//
// Author: Olivier Gillet (pichenettes@mutable-instruments.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Typhoon: the eight pots on the CD4051 (U8), read by ADC2 on PB1. Select
// lines S0-S2 = PE3-PE5, INH = PE2. Adapted from the Supercell pots driver
// of SuperParasites (github.com/patrickdowling/superparasites, MIT).

#ifndef TYPHOON_DRIVERS_POTS_ADC_H_
#define TYPHOON_DRIVERS_POTS_ADC_H_

#include "stmlib/stmlib.h"

namespace monsoon {

// The index is the 4051 channel (X0..X7).
enum PotChannel {
  POT_STEREO,     // X0  STEREO knob  -> BAL
  POT_FEEDBACK,   // X1  FEEDBACK knob -> FBK
  POT_REVERB,     // X2  REVERB knob  -> RVB
  POT_WET,        // X3  WET knob
  POT_POSITION,   // X4  POSITION slider
  POT_TEXTURE,    // X5  TEXTURE slider
  POT_TUNE,       // X6  TUNE knob
  POT_DENSITY,    // X7  DENSITY slider
  POT_LAST
};

class PotsAdc {
 public:
  PotsAdc() { }
  ~PotsAdc() { }

  void Init();
  // Call once per audio block. Calls alternate between "read the finished
  // conversion and set the next mux address" and "start a conversion", so
  // the 4051 has a whole block to settle after each address change (as the
  // stock firmware does). Each pot is refreshed every 16 blocks (~94 Hz).
  void Scan();

  inline float float_value(uint8_t channel) const {
    return static_cast<float>(values_[channel]) / 65536.0f;
  }

 private:
  uint8_t index_;
  bool state_;
  uint16_t values_[POT_LAST];

  DISALLOW_COPY_AND_ASSIGN(PotsAdc);
};

}  // namespace monsoon

#endif  // TYPHOON_DRIVERS_POTS_ADC_H_
