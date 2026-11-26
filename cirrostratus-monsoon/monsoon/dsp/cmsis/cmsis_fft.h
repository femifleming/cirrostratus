/* Plain C entry point to CMSIS-DSP's real FFT (see cmsis_fft.c). */
#ifndef MONSOON_DSP_CMSIS_CMSIS_FFT_H_
#define MONSOON_DSP_CMSIS_CMSIS_FFT_H_
#ifdef __cplusplus
extern "C" {
#endif
/* n = 256, 512, 1024 or 2048. Returns 0 if the size is unsupported.
   CMSIS layout: out = [DC, Nyquist, re1, im1, ...]; `in` is clobbered;
   the inverse includes the 1/n scaling. */
int monsoon_cmsis_rfft(float* in, float* out, int n, int inverse);
#ifdef __cplusplus
}
#endif
#endif
