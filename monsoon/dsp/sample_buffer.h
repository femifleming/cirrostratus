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

// Anti-alias filters for the decimated qualities: 8th-order elliptic
// low-passes (4 biquads, b0 b1 b2 a1 a2), flat to 0.05 dB up to 0.4 x the
// stored rate and 80 dB down from 0.55 x, so nothing aliases into the band
// that is kept. [0] = 2x (24 kHz), [1] = 3x (16 kHz). sample_buffer.cc.
extern const float kDecimatorCoefficients[2][4][5];

class SampleBuffer {
 public:
  // Exponent e means mantissa = x * 2^e * full scale (32767 or 127).
  static const int kMaxExponent = 15;

  // Uses up to `max_bytes` of the arena (leave `reserve` bytes free for
  // the rest of the mode). channels = 1 or 2. `quality` is the master
  // buffer setting (see mode.h): 8-bit mantissas and/or a 2x / 3x lower
  // stored rate. With `decimate` false only the bit depth is used (TAPE
  // does its own resampling).
  //
  // Positions, sizes and heads are always in 48 kHz frames, whatever the
  // stored rate, so the modes don't need to know.
  bool Init(Arena* arena, size_t max_bytes, int channels,
            size_t reserve = 0, uint8_t quality = 0, bool decimate = true) {
    channels_ = channels;
    eight_bit_ = QualityEightBit(quality);
    decimation_ = decimate ? QualityDecimation(quality) : 1;
    inv_decimation_ = 1.0f / static_cast<float>(decimation_);
    lofi_ = eight_bit_ || decimation_ > 1;
    stage_ = NULL;
    if (decimation_ > 1) {
      stage_ = arena->Allocate<float>(kBlockSize * channels);
      if (!stage_) return false;
    }
    const size_t bytes = eight_bit_ ? 1 : 2;
    const size_t block_bytes = channels * kBlockSize * bytes;
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
    data_[0] = static_cast<int16_t*>(arena->Allocate(b0 * block_bytes));
    size_t b1 = blocks - b0;
    if (b1) {
      size_t fit = (arena->largest_free() & ~size_t(7)) / block_bytes;
      if (b1 > fit) b1 = fit;
      data_[1] = b1 ? static_cast<int16_t*>(arena->Allocate(b1 * block_bytes))
                    : NULL;
    } else {
      data_[1] = NULL;
    }
    if (!data_[0]) return false;
    num_blocks_ = b0 + b1;
    split_ = b0 * kBlockSize;
    size_ = num_blocks_ * kBlockSize;
    const float full = eight_bit_ ? 127.0f : 32767.0f;
    for (int e = 0; e <= kMaxExponent; ++e) {
      scale_[e] = 1.0f / (full * static_cast<float>(1 << e));
    }
    Clear();
    return true;
  }

  void Clear() {
    const size_t bytes = eight_bit_ ? 1 : 2;
    memset(data_[0], 0, split_ * channels_ * bytes);
    if (data_[1]) {
      memset(data_[1], 0, (size_ - split_) * channels_ * bytes);
    }
    memset(exponent_, 0, num_blocks_ * channels_);
    write_head_ = 0;
    filled_ = 0;
    staged_ = 0;
    phase_ = 0;
    memset(aa_state_, 0, sizeof(aa_state_));
  }

  // Writes exactly kBlockSize frames (the codec block). Mono buffers store
  // the mid signal.
  void WriteBlock(const FloatFrame* in) {
    if (decimation_ > 1) {
      WriteDecimated(in);
      return;
    }
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
    Advance();
  }

  // Sample at a 48 kHz frame index in [0, size()).
  __attribute__((always_inline)) inline float Sample(int channel, size_t index) const {
    if (__builtin_expect(lofi_, 0)) return ReadLofi(channel, static_cast<float>(index));
    return Sample16(channel, index);
  }

  // Wrapped integer read (48 kHz frames).
  __attribute__((always_inline)) inline float At(int channel, int32_t index) const {
    if (__builtin_expect(lofi_, 0)) return ReadLofi(channel, static_cast<float>(index));
    return At16(channel, index);
  }

  // Hermite-interpolated read at a fractional position (wraps).
  __attribute__((always_inline)) inline float Read(int channel, float position) const {
    if (__builtin_expect(lofi_, 0)) return ReadLofi(channel, position);
    float fl = FastFloor(position);
    int32_t i = static_cast<int32_t>(fl);
    float t = position - fl;
    return Hermite(At16(channel, i - 1), At16(channel, i),
                   At16(channel, i + 1), At16(channel, i + 2), t);
  }

  // The same reads with the quality decided at compile time, for hot
  // loops: a mode checks lofi() once per block and runs a loop built with
  // ReadT<false> (exactly the v1.0 read, no per-sample branch) or
  // ReadT<true>.
  template<bool kLofi>
  __attribute__((always_inline)) inline float ReadT(int channel,
                                                    float position) const {
    if (kLofi) return ReadLofi(channel, position);
    float fl = FastFloor(position);
    int32_t i = static_cast<int32_t>(fl);
    float t = position - fl;
    return Hermite(At16(channel, i - 1), At16(channel, i),
                   At16(channel, i + 1), At16(channel, i + 2), t);
  }
  bool lofi() const { return lofi_; }
  // Sample() without the quality check: only when !lofi().
  __attribute__((always_inline)) inline float SampleFast(
      int channel, size_t index) const {
    return Sample16(channel, index);
  }

  // `n` consecutive 48 kHz frames from index `start` (wraps) into `out`:
  // the fast way for block reads such as STRETCH's analysis window.
  void ReadSpan(int channel, int32_t start, size_t n, float* out) const;

  // `n` consecutive STORED samples from stored index `start` (wraps) into
  // `out`, as floats: the values ReadLofi()/Read() interpolate between (at
  // 48 kHz 16-bit stored samples are 48 kHz frames). For hot loops that
  // interpolate themselves: position / decimation() is the stored position.
  void ReadStored(int channel, int32_t start, size_t n, float* out) const {
    int32_t k = Wrap(start);
    const int32_t size = static_cast<int32_t>(size_);
    if (eight_bit_) {
      for (size_t i = 0; i < n; ++i) {
        out[i] = Sample8(channel, static_cast<size_t>(k));
        if (++k == size) k = 0;
      }
    } else {
      for (size_t i = 0; i < n; ++i) {
        out[i] = Sample16(channel, static_cast<size_t>(k));
        if (++k == size) k = 0;
      }
    }
  }
  float inv_decimation() const { return inv_decimation_; }

  // Position `delay` frames behind the write head.
  float Behind(float delay) const {
    return static_cast<float>(write_head()) - delay;
  }

  // All in 48 kHz frames.
  size_t size() const { return size_ * decimation_; }
  size_t filled() const { return filled_ * decimation_; }
  // Where the last stored block ends. At a lower stored rate the blocks
  // are committed every kBlockSize x decimation frames, so this moves in
  // steps; head() moves on every frame written (frames that are still
  // being decimated count too).
  size_t write_head() const { return write_head_ * decimation_; }
  size_t head() const { return write_head() + staged_ * decimation_ + phase_; }
  // How far behind head() a read must stay to only see stored audio
  // (0 at 48 kHz, where head() == write_head() after every block).
  size_t guard() const {
    return decimation_ > 1 ? (kBlockSize + 4) * decimation_ : 0;
  }
  int channels() const { return channels_; }
  int decimation() const { return decimation_; }
  bool eight_bit() const { return eight_bit_; }
  float seconds() const { return static_cast<float>(size()) / kSampleRate; }

  // ---- Persistence: header + exponents + both data segments.
  struct Header {
    uint32_t size;
    uint32_t write_head;
    uint32_t filled;
    uint32_t channels;  // | 0x100 for 8-bit | decimation << 16 (if > 1)
  };

  // Appends up to 4 chunks (needs 4 free slots). Returns count added.
  size_t GetChunks(PersistentChunk* chunks) {
    header_.size = static_cast<uint32_t>(size_);
    header_.write_head = static_cast<uint32_t>(write_head_);
    header_.filled = static_cast<uint32_t>(filled_);
    header_.channels = format();
    const size_t bytes = eight_bit_ ? 1 : 2;
    size_t n = 0;
    chunks[n].data = &header_; chunks[n++].size = sizeof(header_);
    chunks[n].data = exponent_; chunks[n++].size = num_blocks_ * channels_;
    chunks[n].data = data_[0];
    chunks[n++].size = split_ * channels_ * bytes;
    if (data_[1]) {
      chunks[n].data = data_[1];
      chunks[n++].size = (size_ - split_) * channels_ * bytes;
    }
    return n;
  }

  // Bytes GetChunks writes (each chunk padded to 4 bytes by the slot).
  size_t persistent_bytes() const {
    const size_t bytes = eight_bit_ ? 1 : 2;
    size_t pad = 0;
    pad += (sizeof(Header) + 3) & ~size_t(3);
    pad += (num_blocks_ * channels_ + 3) & ~size_t(3);
    pad += (split_ * channels_ * bytes + 3) & ~size_t(3);
    pad += ((size_ - split_) * channels_ * bytes + 3) & ~size_t(3);
    return pad;
  }

  // Loads what GetChunks saved. Returns bytes consumed, 0 on mismatch.
  size_t Load(const uint8_t* p, size_t available) {
    if (available < sizeof(Header)) return 0;
    Header h;
    memcpy(&h, p, sizeof(h));
    if (h.size != size_ || h.channels != format()) {
      return 0;
    }
    if (persistent_bytes() > available) return 0;
    const size_t bytes = eight_bit_ ? 1 : 2;
    const size_t s0 = split_ * channels_ * bytes;
    const size_t s1 = (size_ - split_) * channels_ * bytes;
    size_t off = (sizeof(Header) + 3) & ~size_t(3);
    memcpy(exponent_, p + off, num_blocks_ * channels_);
    off += (num_blocks_ * channels_ + 3) & ~size_t(3);
    memcpy(data_[0], p + off, s0);
    off += (s0 + 3) & ~size_t(3);
    if (data_[1]) {
      memcpy(data_[1], p + off, s1);
      off += (s1 + 3) & ~size_t(3);
    }
    write_head_ = h.write_head % size_;
    filled_ = h.filled > size_ ? size_ : h.filled;
    staged_ = 0;
    phase_ = 0;
    return off;
  }

 private:
  uint32_t format() const {
    return static_cast<uint32_t>(channels_) | (eight_bit_ ? 0x100u : 0u) |
        (decimation_ > 1 ? static_cast<uint32_t>(decimation_) << 16 : 0u);
  }

  void Advance() {
    write_head_ += kBlockSize;
    if (write_head_ >= size_) write_head_ = 0;
    if (filled_ < size_) filled_ += kBlockSize;
  }

  // Anti-alias filter, decimate, commit every kBlockSize stored frames.
  void WriteDecimated(const FloatFrame* in);  // sample_buffer.cc

  __attribute__((always_inline)) inline float Sample16(int channel, size_t index) const {
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

  inline float Sample8(int channel, size_t index) const {
    if (channels_ == 1) channel = 0;
    const size_t b = index / kBlockSize;
    const int8_t* d = reinterpret_cast<const int8_t*>(data_[0]);
    size_t local = index;
    if (index >= split_) {
      d = reinterpret_cast<const int8_t*>(data_[1]);
      local = index - split_;
    }
    return d[local * channels_ + channel] *
        scale_[exponent_[b * channels_ + channel]];
  }

  __attribute__((always_inline)) inline int32_t Wrap(int32_t index) const {
    int32_t n = static_cast<int32_t>(size_);
    if (index >= n || index < 0) {
      index %= n;
      if (index < 0) index += n;
    }
    return index;
  }

  __attribute__((always_inline)) inline float At16(int channel, int32_t index) const {
    return Sample16(channel, static_cast<size_t>(Wrap(index)));
  }

  inline float At8(int channel, int32_t index) const {
    return Sample8(channel, static_cast<size_t>(Wrap(index)));
  }

  // Read for the 8-bit and/or lower-rate qualities: position in 48 kHz
  // frames, Hermite between the stored samples. Out of line (one copy in
  // sample_buffer.cc) so the 48 kHz 16-bit loops of every mode stay as
  // they were.
  __attribute__((noinline)) float ReadLofi(int channel, float position) const;

  void WriteChannel(const float* src, size_t stride, size_t block, int ch);

  int channels_ = 2;
  int decimation_ = 1;
  float inv_decimation_ = 1.0f;
  bool eight_bit_ = false;
  bool lofi_ = false;
  size_t size_ = 0;       // stored frames (this and below)
  size_t split_ = 0;
  size_t num_blocks_ = 0;
  size_t write_head_ = 0;
  size_t filled_ = 0;
  size_t staged_ = 0;     // stored frames waiting in stage_
  size_t phase_ = 0;      // input frames into the current decimation step
  int16_t* data_[2] = { NULL, NULL };  // int8_t storage when eight_bit_
  uint8_t* exponent_ = NULL;
  float* stage_ = NULL;   // kBlockSize x channels, interleaved
  float aa_state_[2][4][2];
  float scale_[kMaxExponent + 1];
  Header header_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_SAMPLE_BUFFER_H_
