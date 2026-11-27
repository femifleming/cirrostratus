// Typhoon AUX DAC (PA4, DAC channel 1). See dac.h.

#include "typhoon/drivers/dac.h"

#include <stm32f4xx_conf.h>

namespace monsoon {

static void PinLow() {
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Pin = GPIO_Pin_4;
  gpio_init.GPIO_Mode = GPIO_Mode_OUT;
  gpio_init.GPIO_OType = GPIO_OType_PP;
  gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
  gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIOA->BSRRH = GPIO_Pin_4;
  GPIO_Init(GPIOA, &gpio_init);
  GPIOA->BSRRH = GPIO_Pin_4;
}

void Dac::Init() {
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
  enabled_ = true;
  set_enabled(false);
}

void Dac::set_enabled(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  if (on) {
    DAC_InitTypeDef dac_init;
    DAC_StructInit(&dac_init);
    dac_init.DAC_Trigger = DAC_Trigger_None;
    dac_init.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac_init.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &dac_init);
    DAC_SetChannel1Data(DAC_Align_12b_R, 0);
    DAC_Cmd(DAC_Channel_1, ENABLE);
    GPIO_InitTypeDef gpio_init;
    gpio_init.GPIO_Pin = GPIO_Pin_4;
    gpio_init.GPIO_Mode = GPIO_Mode_AN;
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &gpio_init);
  } else {
    PinLow();
    DAC_Cmd(DAC_Channel_1, DISABLE);
  }
}

void Dac::Write(uint16_t code) {
  if (!enabled_) return;
  DAC_SetChannel1Data(DAC_Align_12b_R, code > 4095 ? 4095 : code);
}

}  // namespace monsoon
