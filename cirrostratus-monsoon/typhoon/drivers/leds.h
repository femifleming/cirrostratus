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
// Typhoon LEDs: 16 single-colour LEDs plus the FREEZE button LED, each on its
// own GPIO (anode to the pin through a resistor, assumed active high). Pin
// map from SuperParasites' Supercell driver (MIT) and the Typhoon netlist:
//
//   TIME  "1 2 4 8"  PD0  PD1  PD2  PD3    (right column, above TIME, top to bottom)
//   BANK  "A B C D"  PD4  PD5  PD6  PD7    (left column, above BANK, top to bottom)
//   IN  meter        PD12 PD13 PD14 PD15   (green green yellow red)
//   OUT meter        PC9  PC8  PC7  PC6    (green green yellow red)
//   FREEZE           PD10
//
// Brightness: 8-step software PWM from the 1 kHz tick (as the Monsoon).

#ifndef TYPHOON_DRIVERS_LEDS_H_
#define TYPHOON_DRIVERS_LEDS_H_

#include <stm32f4xx_conf.h>

#include "stmlib/stmlib.h"

namespace monsoon {

enum LedGroup {
  LED_TIME,
  LED_BANK,
  LED_IN,
  LED_OUT,
  kNumLedGroups
};

const uint8_t kLedsPerGroup = 4;

class Leds {
 public:
  Leds() { }
  ~Leds() { }

  void Init();

  void set(uint8_t group, uint8_t index, uint8_t level) {
    level_[group][index] = level;
  }
  uint8_t get(uint8_t group, uint8_t index) const {
    return level_[group][index];
  }
  void set_group(uint8_t group, uint8_t level) {
    for (uint8_t i = 0; i < kLedsPerGroup; ++i) level_[group][i] = level;
  }

  // A 4-LED level bar (0..32767), same thresholds as the Monsoon's bar:
  // each LED takes a quarter and fades in over it.
  void PaintBar(uint8_t group, int32_t bar) {
    if (bar < 0) return;
    if (bar > 32767) bar = 32767;
    bar <<= 1;
    for (uint8_t i = 0; i < kLedsPerGroup; ++i) {
      int32_t lo = i * 16384;
      if (bar >= lo + 16384) {
        level_[group][i] = 255;
      } else if (bar > lo) {
        level_[group][i] = static_cast<uint8_t>((bar - lo) >> 6);
      }
    }
  }

  void set_freeze(bool status) { freeze_led_ = status; }

  void Clear();
  void Write();

 private:
  bool freeze_led_;
  uint8_t pwm_counter_;
  uint8_t level_[kNumLedGroups][kLedsPerGroup];

  DISALLOW_COPY_AND_ASSIGN(Leds);
};

}  // namespace monsoon

#endif  // TYPHOON_DRIVERS_LEDS_H_
