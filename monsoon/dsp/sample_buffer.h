// Block-floating-point audio ring buffer.
//
// Samples are stored as int16 mantissas, and every block of 32 samples
// carries its own power-of-two exponent. A quiet passage therefore keeps
// almost the full 16 bits of resolution, which is what lets a frozen,
// faded, or re-pitched buffer stay clean at 48 kHz without spending 4 bytes
// per sample.
//
// The buffer may span two memory regions (main SRAM + CCM) so that it can
// use all of the RAM a mode has left.

#ifndef MONSOON_DSP_SAMPLE_BUFFER_H_
#define MONSOON_DSP_SAMPLE_BUFFER_H_

#include "monsoon/dsp/core.h"
#include "monsoon/dsp/mode.h"

namespace monsoon {

class SampleBuffer {
 public:
  // Exponent e means mantissa = x * 2^e * 32767.
  static const int kMaxExponent = 15;

  // Uses up to `max_bytes` of the arena (leave `reserve` bytes free for
  // the rest of the mode). channels = 1 or 2.
  bool Init(Arena* arena, size_t max_bytes, int channels,
            size_t reserve = 0) {
    channels_ = channels;
    const size_t block_bytes = channels * kBlockSize * 2;
    const size_t per_block = block_bytes + channels;  // + exponents
    size_t avail = arena->free(0) + arena->free(1);
    avail = avail > reserve + 64 ? avail - reserve - 64 : 0;
    if (avail > max_bytes) avail = max_bytes;
    size_t blocks = avail / per_block;
    if (blocks < 8) return false;
    exponent_ = arena->Allocate<uint8_t>(blocks * channels);
    if (!exponent_) return false;
    // First segment: as much as fits in the largest region.
    size_t b0 = (arena->largest_free() & ~size_t(7)) / block_bytes;
    if (b0 > blocks) b0 = blocks;
    data_[0] = arena->Allocate<int16_t>(b0 * kBlockSize * channels);
    size_t b1 = blocks - b0;
    if (b1) {
      size_t fit = (arena->largest_free() & ~size_t(7)) / block_bytes;
      if (b1 > fit) b1 = fit;
      data_[1] = b1 ? arena->Allocate<int16_t>(b1 * kBlockSize * channels)
                    : NULL;
    } else {
      data_[1] = NULL;
    }
    if (!data_[0]) return false;
    num_blocks_ = b0 + b1;
    split_ = b0 * kBlockSize;
    size_ = num_blocks_ * kBlockSize;
    for (int e = 0; e <= kMaxExponent; ++e) {
      scale_[e] = 1.0f / (32767.0f * static_cast<float>(1 << e));
    }
    Clear();
    return true;
  }

  void Clear() {
    memset(data_[0], 0, split_ * channels_ * sizeof(int16_t));
    if (data_[1]) {
      memset(data_[1], 0, (size_ - split_) * channels_ * sizeof(int16_t));
    }
    memset(exponent_, 0, num_blocks_ * channels_);
    write_head_ = 0;
    filled_ = 0;
  }

  // Writes exactly kBlockSize frames (the codec block). Mono buffers store
  // the mid signal.
  void WriteBlock(const FloatFrame* in) {
    size_t block = write_head_ / kBlockSize;
    if (channels_ == 1) {
      float tmp[kBlockSize];
      for (size_t i = 0; i < kBlockSize; ++i) {
        tmp[i] = 0.5f * (in[i].l + in[i].r);
      }
      WriteChannel(tmp, 1, block, 0);
    } else {
      WriteChannel(&in[0].l, 2, block, 0);
      WriteChannel(&in[0].r, 2, block, 1);
    }
    write_head_ += kBlockSize;
    if (write_head_ >= size_) write_head_ = 0;
    if (filled_ < size_) filled_ += kBlockSize;
  }

  inline float Sample(int channel, size_t index) const {
    if (channels_ == 1) channel = 0;
    const size_t b = index / kBlockSize;
    const int16_t* d = data_[0];
    size_t local = index;
    if (index >= split_) {
      d = data_[1];
      local = index - split_;
    }
    return d[local * channels_ + channel] *
        scale_[exponent_[b * channels_ + channel]];
  }

  // Wrapped integer read.
  inline float At(int channel, int32_t index) const {
    int32_t n = static_cast<int32_t>(size_);
    if (index >= n || index < 0) {
      index %= n;
      if (index < 0) index += n;
    }
    return Sample(channel, static_cast<size_t>(index));
  }

  // Hermite-interpolated read at a fractional position (wraps).
  inline float Read(int channel, float position) const {
    float fl = floorf(position);
    int32_t i = static_cast<int32_t>(fl);
    float t = position - fl;
    return Hermite(At(channel, i - 1), At(channel, i), At(channel, i + 1),
                   At(channel, i + 2), t);
  }

  // Position `delay` frames behind the write head.
  float Behind(float delay) const {
    return static_cast<float>(write_head_) - delay;
  }

  size_t size() const { return size_; }
  size_t filled() const { return filled_; }
  size_t write_head() const { return write_head_; }
  int channels() const { return channels_; }
  float seconds() const { return static_cast<float>(size_) / kSampleRate; }

  // ---- Persistence: header + exponents + both data segments.
  struct Header {
    uint32_t size;
    uint32_t write_head;
    uint32_t filled;
    uint32_t channels;
  };

  // Appends up to 4 chunks (needs 4 free slots). Returns count added.
  size_t GetChunks(PersistentChunk* chunks) {
    header_.size = static_cast<uint32_t>(size_);
    header_.write_head = static_cast<uint32_t>(write_head_);
    header_.filled = static_cast<uint32_t>(filled_);
    header_.channels = static_cast<uint32_t>(channels_);
    size_t n = 0;
    chunks[n].data = &header_; chunks[n++].size = sizeof(header_);
    chunks[n].data = exponent_; chunks[n++].size = num_blocks_ * channels_;
    chunks[n].data = data_[0];
    chunks[n++].size = split_ * channels_ * sizeof(int16_t);
    if (data_[1]) {
      chunks[n].data = data_[1];
      chunks[n++].size = (size_ - split_) * channels_ * sizeof(int16_t);
    }
    return n;
  }

  // Bytes GetChunks writes (each chunk padded to 4 bytes by the slot).
  size_t persistent_bytes() const {
    size_t pad = 0;
    pad += (sizeof(Header) + 3) & ~size_t(3);
    pad += (num_blocks_ * channels_ + 3) & ~size_t(3);
    pad += split_ * channels_ * 2;
    pad += (size_ - split_) * channels_ * 2;
    return pad;
  }

  // Loads what GetChunks saved. Returns bytes consumed, 0 on mismatch.
  size_t Load(const uint8_t* p, size_t available) {
    if (available < sizeof(Header)) return 0;
    Header h;
    memcpy(&h, p, sizeof(h));
    if (h.size != size_ || h.channels != static_cast<uint32_t>(channels_)) {
      return 0;
    }
    if (persistent_bytes() > available) return 0;
    size_t off = (sizeof(Header) + 3) & ~size_t(3);
    memcpy(exponent_, p + off, num_blocks_ * channels_);
    off += (num_blocks_ * channels_ + 3) & ~size_t(3);
    memcpy(data_[0], p + off, split_ * channels_ * 2);
    off += split_ * channels_ * 2;
    if (data_[1]) {
      memcpy(data_[1], p + off, (size_ - split_) * channels_ * 2);
      off += (size_ - split_) * channels_ * 2;
    }
    write_head_ = h.write_head % size_;
    filled_ = h.filled > size_ ? size_ : h.filled;
    return off;
  }

 private:
  void WriteChannel(const float* src, size_t stride, size_t block, int ch) {
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
    const float gain = 32767.0f * static_cast<float>(1 << e);
    size_t first = block * kBlockSize;
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

  int channels_ = 2;
  size_t size_ = 0;
  size_t split_ = 0;
  size_t num_blocks_ = 0;
  size_t write_head_ = 0;
  size_t filled_ = 0;
  int16_t* data_[2] = { NULL, NULL };
  uint8_t* exponent_ = NULL;
  float scale_[kMaxExponent + 1];
  Header header_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_SAMPLE_BUFFER_H_
