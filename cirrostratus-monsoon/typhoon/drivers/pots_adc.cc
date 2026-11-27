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
// Typhoon pot mux (adapted from SuperParasites' Supercell pots driver).

#include "typhoon/drivers/pots_adc.h"

#include <stm32f4xx_conf.h>

namespace monsoon {

void PotsAdc::Init() {
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOE, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC2, ENABLE);

  ADC_InitTypeDef adc_init;
  GPIO_InitTypeDef gpio_init;

  // Mux output on PB1 (ADC12_IN9).
  gpio_init.GPIO_Pin = GPIO_Pin_1;
  gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
  gpio_init.GPIO_Mode = GPIO_Mode_AN;
  GPIO_Init(GPIOB, &gpio_init);

  // INH (PE2) and the address lines S0-S2 (PE3-PE5).
  gpio_init.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
  gpio_init.GPIO_Mode = GPIO_Mode_OUT;
  gpio_init.GPIO_OType = GPIO_OType_PP;
  gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
  gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_Init(GPIOE, &gpio_init);
  GPIO_ResetBits(GPIOE, GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5);
  GPIO_ResetBits(GPIOE, GPIO_Pin_2);  // enabled, channel X0

  // ADC2 shares the common settings (prescaler) ADC1 set up in Adc::Init.
  adc_init.ADC_Resolution = ADC_Resolution_12b;
  adc_init.ADC_ScanConvMode = DISABLE;
  adc_init.ADC_ContinuousConvMode = DISABLE;
  adc_init.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T1_CC1;
  adc_init.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
  adc_init.ADC_DataAlign = ADC_DataAlign_Left;
  adc_init.ADC_NbrOfConversion = 1;
  ADC_Init(ADC2, &adc_init);
  ADC_RegularChannelConfig(ADC2, ADC_Channel_9, 1, ADC_SampleTime_480Cycles);
  ADC_Cmd(ADC2, ENABLE);

  for (int i = 0; i < POT_LAST; ++i) values_[i] = 0;
  index_ = 0;
  state_ = false;  // the next Scan() starts the conversion of X0
}

void PotsAdc::Scan() {
  if (state_) {
    // Read the finished conversion, then point the mux at the next pot.
    values_[index_] = ADC2->DR;
    index_ = (index_ + 1) % POT_LAST;
    uint32_t set = 0, reset = 0;
    (index_ & 1 ? set : reset) |= GPIO_Pin_3;
    (index_ & 2 ? set : reset) |= GPIO_Pin_4;
    (index_ & 4 ? set : reset) |= GPIO_Pin_5;
    GPIOE->BSRRL = GPIO_Pin_2;  // inhibit while the address changes
    GPIOE->BSRRL = set;
    GPIOE->BSRRH = reset;
    GPIOE->BSRRH = GPIO_Pin_2;
  } else {
    ADC_SoftwareStartConv(ADC2);
  }
  state_ = !state_;
}

}  // namespace monsoon
