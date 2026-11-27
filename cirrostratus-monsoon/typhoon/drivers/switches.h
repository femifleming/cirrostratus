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
// Typhoon front panel buttons: six switches on PE7-PE12 that connect the pin
// to +3.3 V (active high, internal pull-downs). Pins from SuperParasites'
// Supercell driver (MIT) and the Typhoon netlist.

#ifndef TYPHOON_DRIVERS_SWITCHES_H_
#define TYPHOON_DRIVERS_SWITCHES_H_

#include "stmlib/stmlib.h"

#include <stm32f4xx_conf.h>

// Which mute button is which. The schematic's net names say PE7 = IN mute,
// the stock firmware (and the part placement) say PE8 = IN mute, PE7 = OUT
// mute; we follow the stock firmware. Set to 1 to swap them.
#ifndef TYPHOON_SWAP_MUTE_BUTTONS
#define TYPHOON_SWAP_MUTE_BUTTONS 0
#endif

namespace monsoon {

// The first three keep the Monsoon's order (DISP, WRITE, FREEZE), so the
// Monsoon UI logic carries over unchanged.
enum SwitchIndex {
  SWITCH_DISP,      // TIME   PE11
  SWITCH_WRITE,     // BANK   PE12
  SWITCH_FREEZE,    // FREEZE PE10 (the white LED button, "HOLD")
  SWITCH_TRIG,      // TRIG   PE9
  SWITCH_MUTE_IN,   // IN MUTE  (PE8, see TYPHOON_SWAP_MUTE_BUTTONS)
  SWITCH_MUTE_OUT,  // OUT MUTE (PE7)
  kNumSwitches
};

class Switches {
 public:
  Switches() { }
  ~Switches() { }

  void Init();
  void Debounce();

  inline bool released(uint8_t index) const {
    return switch_state_[index] == 0x7f;
  }

  inline bool just_pressed(uint8_t index) const {
    return switch_state_[index] == 0x80;
  }

  inline bool pressed(uint8_t index) const {
    return switch_state_[index] == 0x00;
  }

  // The raw pin, true while the button is held (unlike the stock driver,
  // whose helper returns the inverse).
  inline bool pressed_immediate(uint8_t index) const {
    return GPIO_ReadInputDataBit(GPIOE, pin(index));
  }

  static inline uint16_t pin(uint8_t index) {
    static const uint16_t pins[kNumSwitches] = {
      GPIO_Pin_11, GPIO_Pin_12, GPIO_Pin_10, GPIO_Pin_9,
#if TYPHOON_SWAP_MUTE_BUTTONS
      GPIO_Pin_7, GPIO_Pin_8
#else
      GPIO_Pin_8, GPIO_Pin_7
#endif
    };
    return pins[index];
  }

 private:
  // 0x00 = held for 8 ms, 0xff = released for 8 ms (as on the Monsoon).
  uint8_t switch_state_[kNumSwitches];

  DISALLOW_COPY_AND_ASSIGN(Switches);
};

}  // namespace monsoon

#endif  // TYPHOON_DRIVERS_SWITCHES_H_
