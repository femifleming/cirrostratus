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
// Typhoon LEDs (pins from SuperParasites' Supercell driver).

#include "typhoon/drivers/leds.h"

namespace monsoon {

// Pin of each LED, per group; all on port D except the OUT meter (port C).
static const uint16_t kPins[kNumLedGroups][kLedsPerGroup] = {
  { GPIO_Pin_0, GPIO_Pin_1, GPIO_Pin_2, GPIO_Pin_3 },      // TIME 1 2 4 8
  { GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_6, GPIO_Pin_7 },      // BANK A B C D
  { GPIO_Pin_12, GPIO_Pin_13, GPIO_Pin_14, GPIO_Pin_15 },  // IN meter
  { GPIO_Pin_9, GPIO_Pin_8, GPIO_Pin_7, GPIO_Pin_6 },      // OUT meter
};
static const uint16_t kPinFreezeLed = GPIO_Pin_10;
// PD0-7, PD10, PD12-15.
static const uint16_t kPortDPins = 0x00ff | GPIO_Pin_10 | 0xf000;
static const uint16_t kPortCPins = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 |
                                   GPIO_Pin_9;

void Leds::Init() {
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, ENABLE);

  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Mode = GPIO_Mode_OUT;
  gpio_init.GPIO_OType = GPIO_OType_PP;
  gpio_init.GPIO_Speed = GPIO_Speed_25MHz;
  gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
  gpio_init.GPIO_Pin = kPortDPins;
  GPIO_Init(GPIOD, &gpio_init);
  // PC6 is the OUT meter's red LED here (the Monsoon code made it I2S MCK).
  gpio_init.GPIO_Pin = kPortCPins;
  GPIO_Init(GPIOC, &gpio_init);

  Clear();
  pwm_counter_ = 0;
  Write();
}

void Leds::Write() {
  pwm_counter_ += 32;
  uint16_t on_d = 0, on_c = 0;
  for (uint8_t g = 0; g < kNumLedGroups; ++g) {
    for (uint8_t i = 0; i < kLedsPerGroup; ++i) {
      uint8_t v = level_[g][i];
      if (v && v >= pwm_counter_) {
        if (g == LED_OUT) {
          on_c |= kPins[g][i];
        } else {
          on_d |= kPins[g][i];
        }
      }
    }
  }
  if (freeze_led_) on_d |= kPinFreezeLed;
  GPIOD->BSRRL = on_d;
  GPIOD->BSRRH = kPortDPins & ~on_d;
  GPIOC->BSRRL = on_c;
  GPIOC->BSRRH = kPortCPins & ~on_c;
}

void Leds::Clear() {
  for (uint8_t g = 0; g < kNumLedGroups; ++g) {
    for (uint8_t i = 0; i < kLedsPerGroup; ++i) level_[g][i] = 0;
  }
  freeze_led_ = false;
}

}  // namespace monsoon
