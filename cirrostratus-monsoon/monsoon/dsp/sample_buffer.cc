// SampleBuffer: the decimating write path for the lower-rate qualities.

#include "monsoon/dsp/sample_buffer.h"

namespace monsoon {

// Designed with scipy.signal.ellip(8, 0.05, 80, 0.8 / D), as SOS.
const float kDecimatorCoefficients[2][4][5] = {
  {  // 2x: 24 kHz stored
    { 0.00539677609f, 0.00983578679f, 0.00539677609f, -0.989206207f, 0.290456111f },
    { 1.0f, 0.975196022f, 1.0f, -0.80817372f, 0.48737368f },
    { 1.0f, 0.373954944f, 1.0f, -0.618505638f, 0.720648903f },
    { 1.0f, 0.127658767f, 1.0f, -0.529269911f, 0.911921403f },
  },
  {  // 3x: 16 kHz stored
    { 0.00127307143f, 0.00198584568f, 0.00127307143f, -1.32817839f, 0.466883351f },
    { 1.0f, 0.0863369438f, 1.0f, -1.28377502f, 0.604717186f },
    { 1.0f, -0.583550606f, 1.0f, -1.24394672f, 0.778785668f },
    { 1.0f, -0.803598424f, 1.0f, -1.25276031f, 0.929636886f },
  },
};

float SampleBuffer::ReadLofi(int channel, float position) const {
  const float p = position * inv_decimation_;
  float fl = FastFloor(p);
  int32_t i = static_cast<int32_t>(fl);
  float t = p - fl;
  if (eight_bit_) {
    return Hermite(At8(channel, i - 1), At8(channel, i),
                   At8(channel, i + 1), At8(channel, i + 2), t);
  }
  return Hermite(At16(channel, i - 1), At16(channel, i),
                 At16(channel, i + 1), At16(channel, i + 2), t);
}

void SampleBuffer::WriteChannel(const float* src, size_t stride, size_t block, int ch) {
  float peak = 0.0f;
  for (size_t i = 0; i < kBlockSize; ++i) {
    float a = fabsf(src[i * stride]);
    if (a > peak) peak = a;
  }
  // Largest exponent that keeps peak * 2^e <= 1.
  int e = 0;
  if (peak > 1.0f) peak = 1.0f;
  while (e < kMaxExponent && peak * static_cast<float>(2 << e) <= 1.0f) {
    ++e;
  }
  exponent_[block * channels_ + ch] = static_cast<uint8_t>(e);
  size_t first = block * kBlockSize;
  if (eight_bit_) {
    const float gain = 127.0f * static_cast<float>(1 << e);
    int8_t* dst = first >= split_
        ? &reinterpret_cast<int8_t*>(data_[1])[(first - split_) * channels_ + ch]
        : &reinterpret_cast<int8_t*>(data_[0])[first * channels_ + ch];
    for (size_t i = 0; i < kBlockSize; ++i) {
      float v = Clamp(src[i * stride], -1.0f, 1.0f) * gain;
      int32_t q = static_cast<int32_t>(v + (v >= 0.0f ? 0.5f : -0.5f));
      if (q > 127) q = 127;
      if (q < -127) q = -127;
      dst[i * channels_] = static_cast<int8_t>(q);
    }
    return;
  }
  const float gain = 32767.0f * static_cast<float>(1 << e);
  int16_t* dst = first >= split_
      ? &data_[1][(first - split_) * channels_ + ch]
      : &data_[0][first * channels_ + ch];
  for (size_t i = 0; i < kBlockSize; ++i) {
    float v = Clamp(src[i * stride], -1.0f, 1.0f) * gain;
    int32_t q = static_cast<int32_t>(v + (v >= 0.0f ? 0.5f : -0.5f));
    if (q > 32767) q = 32767;
    if (q < -32767) q = -32767;
    dst[i * channels_] = static_cast<int16_t>(q);
  }
}

void SampleBuffer::ReadSpan(int channel, int32_t start, size_t n,
                            float* out) const {
  if (!lofi_) {
    int32_t k = Wrap(start);
    const int32_t size = static_cast<int32_t>(size_);
    for (size_t i = 0; i < n; ++i) {
      out[i] = Sample16(channel, static_cast<size_t>(k));
      if (++k == size) k = 0;
    }
    return;
  }
  // Walk the stored samples once: position start / D, stepping 1 / D.
  const int d = decimation_;
  int32_t base = start >= 0 ? start / d : -((-start + d - 1) / d);
  int32_t frac = start - base * d;  // 0 .. d-1
  const int32_t size = static_cast<int32_t>(size_);
  int32_t k = Wrap(base - 1);
  float x[4];
  for (int j = 0; j < 4; ++j) {
    x[j] = eight_bit_ ? Sample8(channel, static_cast<size_t>(k))
                      : Sample16(channel, static_cast<size_t>(k));
    if (++k == size) k = 0;
  }
  // k now indexes the stored sample after x[3].
  for (size_t i = 0; i < n; ++i) {
    out[i] = d == 1 ? x[1]
                    : Hermite(x[0], x[1], x[2], x[3],
                              static_cast<float>(frac) * inv_decimation_);
    if (++frac == d) {
      frac = 0;
      x[0] = x[1]; x[1] = x[2]; x[2] = x[3];
      x[3] = eight_bit_ ? Sample8(channel, static_cast<size_t>(k))
                        : Sample16(channel, static_cast<size_t>(k));
      if (++k == size) k = 0;
    }
  }
}

void SampleBuffer::WriteDecimated(const FloatFrame* in) {
  const float (*k)[5] = kDecimatorCoefficients[decimation_ == 2 ? 0 : 1];
  for (size_t i = 0; i < kBlockSize; ++i) {
    float x[2];
    if (channels_ == 1) {
      x[0] = 0.5f * (in[i].l + in[i].r);
    } else {
      x[0] = in[i].l;
      x[1] = in[i].r;
    }
    for (int ch = 0; ch < channels_; ++ch) {
      float v = x[ch];
      for (int s = 0; s < 4; ++s) {
        // Transposed direct form II.
        float* z = aa_state_[ch][s];
        const float y = k[s][0] * v + z[0];
        z[0] = k[s][1] * v - k[s][3] * y + z[1];
        z[1] = k[s][2] * v - k[s][4] * y;
        v = y;
      }
      x[ch] = v;
    }
    if (++phase_ < static_cast<size_t>(decimation_)) continue;
    phase_ = 0;
    for (int ch = 0; ch < channels_; ++ch) {
      stage_[staged_ * channels_ + ch] = x[ch];
    }
    if (++staged_ == kBlockSize) {
      staged_ = 0;
      const size_t block = write_head_ / kBlockSize;
      for (int ch = 0; ch < channels_; ++ch) {
        WriteChannel(stage_ + ch, channels_, block, ch);
      }
      Advance();
    }
  }
}

}  // namespace monsoon
