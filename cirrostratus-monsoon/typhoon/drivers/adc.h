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
// Typhoon: ADC1 scans the CV jacks, the SIZE slider and the OUT-level sense
// (12 channels, DMA). Channel map from the Supercell driver of
// SuperParasites (github.com/patrickdowling/superparasites, MIT), which
// matches the Typhoon netlist. The other seven pots and TUNE sit on a 4051
// read by ADC2 (pots_adc.h). PA4 is the DAC (AUX normal), not an input.

#ifndef TYPHOON_DRIVERS_ADC_H_
#define TYPHOON_DRIVERS_ADC_H_

#include "stmlib/stmlib.h"

namespace monsoon {

// Same order as the stock firmware (its calibration offsets use it).
enum AdcChannel {
  ADC_POSITION_CV,     // PA0  IN0
  ADC_DENSITY_CV,      // PA3  IN3
  ADC_SIZE_POT,        // PC1  IN11 (the SIZE slider, read directly)
  ADC_SIZE_CV,         // PA1  IN1
  ADC_FREQ_CV,         // PA2  IN2  (FREQ jack, +-12 semitones)
  ADC_STEREO_CV,       // PA7  IN7
  ADC_FEEDBACK_CV,     // PA6  IN6
  ADC_REVERB_CV,       // PC2  IN12
  ADC_WET_CV,          // PB0  IN8
  ADC_TEXTURE_CV,      // PA5  IN5
  ADC_V_OCT_CV,        // PC0  IN10
  ADC_VCA_OUT_LEVEL,   // PC4  IN14 (OUT LEVEL + OUT VCA control voltage)
  ADC_CHANNEL_LAST
};

class Adc {
 public:
  Adc() { }
  ~Adc() { }

  void Init();
  void Convert();
  inline uint16_t value(uint8_t channel) const { return values_[channel]; }
  inline float float_value(uint8_t channel) const {
    return static_cast<float>(values_[channel]) / 65536.0f;
  }

 private:
  uint16_t values_[ADC_CHANNEL_LAST];

  DISALLOW_COPY_AND_ASSIGN(Adc);
};

}  // namespace monsoon

#endif  // TYPHOON_DRIVERS_ADC_H_
