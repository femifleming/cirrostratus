/* CMSIS-DSP arm_rfft_fast_f32 built as one C translation unit, so the
   firmware and the host tests use exactly the same FFT code. */
#ifndef ARM_MATH_CM4
#define ARM_MATH_CM4
#endif
#ifndef __FPU_PRESENT
#define __FPU_PRESENT 1
#endif

#include "stmlib/third_party/STM/CMSIS/CM3_f4xx/arm_math.h"
#include "stmlib/third_party/STM/CMSIS/CM3_f4xx/arm_common_tables.h"
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
    /* Like arm_rfft_fast_init_f32, but only for the four sizes Cirrostratus
       uses, so the linker can drop the tables for every other size
       (about 70 KB of flash). */
    arm_rfft_fast_instance_f32* S = &instances[i];
    arm_cfft_instance_f32* c = &S->Sint;
    S->fftLenRFFT = (uint16_t)n;
    c->fftLen = (uint16_t)(n / 2);
    switch (n) {
      case 256:
        c->bitRevLength = ARMBITREVINDEXTABLE_128_TABLE_LENGTH;
        c->pBitRevTable = (uint16_t*)armBitRevIndexTable128;
        c->pTwiddle = (float32_t*)twiddleCoef_128;
        S->pTwiddleRFFT = (float32_t*)twiddleCoef_rfft_256;
        break;
      case 512:
        c->bitRevLength = ARMBITREVINDEXTABLE_256_TABLE_LENGTH;
        c->pBitRevTable = (uint16_t*)armBitRevIndexTable256;
        c->pTwiddle = (float32_t*)twiddleCoef_256;
        S->pTwiddleRFFT = (float32_t*)twiddleCoef_rfft_512;
        break;
      case 1024:
        c->bitRevLength = ARMBITREVINDEXTABLE_512_TABLE_LENGTH;
        c->pBitRevTable = (uint16_t*)armBitRevIndexTable512;
        c->pTwiddle = (float32_t*)twiddleCoef_512;
        S->pTwiddleRFFT = (float32_t*)twiddleCoef_rfft_1024;
        break;
      default:  /* 2048 */
        c->bitRevLength = ARMBITREVINDEXTABLE1024_TABLE_LENGTH;
        c->pBitRevTable = (uint16_t*)armBitRevIndexTable1024;
        c->pTwiddle = (float32_t*)twiddleCoef_1024;
        S->pTwiddleRFFT = (float32_t*)twiddleCoef_rfft_2048;
        break;
    }
    ready[i] = 1;
  }
  arm_rfft_fast_f32(&instances[i], in, out, (uint8_t)inverse);
  return 1;
}

#include "stmlib/third_party/STM/CMSIS/DSP_Lib/TransformFunctions/arm_cfft_radix8_f32.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/TransformFunctions/arm_cfft_f32.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/TransformFunctions/arm_rfft_fast_f32.c"
#include "stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.c"
