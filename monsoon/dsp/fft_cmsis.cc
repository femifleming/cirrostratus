// Real FFT backend: ARM's CMSIS-DSP arm_rfft_fast_f32 (radix-8 butterflies,
// ~4x faster than ShyFFT on the Cortex-M4), presented in ShyFFT's layout so
// the modes don't care which one runs:
//   out[k] = Re X[k], out[N/2 + k] = -Im X[k] (k = 1..N/2-1),
//   out[0] = DC, out[N/2] = Nyquist; forward + inverse has a gain of N;
//   both calls may overwrite their input buffer.
// The CMSIS code itself is compiled as C in cmsis/cmsis_fft.c.

#include "monsoon/dsp/stft.h"

#include "monsoon/dsp/cmsis/cmsis_fft.h"

namespace monsoon {

void FftDirect(float* in, float* out, size_t n) {
  if (!monsoon_cmsis_rfft(in, out, static_cast<int>(n), 0)) {
    ShyFftDirect(in, out, n);  // other sizes
    return;
  }
  // CMSIS: out = [DC, Nyq, re1, im1, re2, im2, ...]. `in` is scratch now.
  const size_t h = n / 2;
  in[0] = out[0];
  in[h] = out[1];
  for (size_t k = 1; k < h; ++k) {
    in[k] = out[2 * k];
    in[h + k] = -out[2 * k + 1];
  }
  memcpy(out, in, n * sizeof(float));
}

void FftInverse(float* in, float* out, size_t n) {
  if (n != 256 && n != 512 && n != 1024 && n != 2048) {
    ShyFftInverse(in, out, n);
    return;
  }
  const size_t h = n / 2;
  out[0] = in[0];
  out[1] = in[h];
  for (size_t k = 1; k < h; ++k) {
    out[2 * k] = in[k];
    out[2 * k + 1] = -in[h + k];
  }
  monsoon_cmsis_rfft(out, in, static_cast<int>(n), 1);  // includes 1/N
  const float g = static_cast<float>(n);
  for (size_t i = 0; i < n; ++i) out[i] = in[i] * g;
}

}  // namespace monsoon

