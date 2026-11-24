/* CMSIS-DSP arm_rfft_fast_f32 built as one C translation unit, so the
   firmware and the host tests use exactly the same FFT code. */
#ifndef ARM_MATH_CM4
#define ARM_MATH_CM4
#endif
#ifndef __FPU_PRESENT
#define __FPU_PRESENT 1
#endif

#include "stmlib/third_party/STM/CMSIS/CM3_f4xx/arm_math.h"
#include "stmlib/third_party/STM/CMSIS/CM3_f4xx/arm_const_structs.h"
#include "monsoon/dsp/cmsis/cmsis_fft.h"

/* C version of the assembly arm_bitreversal_32 (same algorithm). */
void arm_bitreversal_32(uint32_t* src, const uint16_t len, const uint16_t* table) {
  uint32_t i;
  for (i = 0; i < len; i += 2) {
    uint32_t a = table[i] >> 2;
    uint32_t b = table[i + 1] >> 2;
    uint32_t t = src[a]; src[a] = src[b]; src[b] = t;
    t = src[a + 1]; src[a + 1] = src[b + 1]; src[b + 1] = t;
  }
}

static arm_rfft_fast_instance_f32 instances[4];
static int ready[4];

int monsoon_cmsis_rfft(float* in, float* out, int n, int inverse) {
  int i = n == 256 ? 0 : n == 512 ? 1 : n == 1024 ? 2 : n == 2048 ? 3 : -1;
  if (i < 0) return 0;
  if (!ready[i]) {
    arm_rfft_fast_init_f32(&instances[i], (uint16_t)n);
    ready[i] = 1;
  }
  arm_rfft_fast_f32(&instances[i], in, out, (uint8_t)inverse);
  return 1;
}

#include "stmlib/third_party/STM/CMSIS/DSP_Lib/TransformFunctions/arm_cfft_radix8_f32.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/TransformFunctions/arm_cfft_f32.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/TransformFunctions/arm_rfft_fast_f32.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/TransformFunctions/arm_rfft_fast_init_f32.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.c"
