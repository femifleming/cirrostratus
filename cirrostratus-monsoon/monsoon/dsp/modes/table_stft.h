// A leaner variant of Stft (monsoon/dsp/stft.h) for QUELL and PUTZ: the
// sqrt-Hann (sine) window is a table in the arena instead of two FastSin
// calls per sample, which saves ~600 instructions per frame at 4x overlap.
// Same ring-buffer scheme, same latency (fft_size + hop), same StftClient
// interface and spectrum layout. Works for hop = N/2 or N/4.

#ifndef MONSOON_DSP_MODES_TABLE_STFT_H_
#define MONSOON_DSP_MODES_TABLE_STFT_H_

#include "monsoon/dsp/core.h"
#include "monsoon/dsp/stft.h"

namespace monsoon {

class TableStft {
 public:
  bool Init(Arena* arena, size_t fft_size, size_t hop, StftClient* client) {
    fft_size_ = fft_size;
    hop_ = hop;
    client_ = client;
    ring_size_ = fft_size + 2 * hop;
    for (int ch = 0; ch < 2; ++ch) {
      in_[ch] = arena->Allocate<float>(ring_size_);
      out_[ch] = arena->Allocate<float>(ring_size_);
      work_[ch] = arena->Allocate<float>(fft_size);
      if (!in_[ch] || !out_[ch] || !work_[ch]) return false;
    }
    temp_ = arena->Allocate<float>(fft_size);
    window_ = arena->Allocate<float>(fft_size);
    if (!temp_ || !window_) return false;
    for (size_t i = 0; i < fft_size; ++i) {
      window_[i] = FastSin(0.5f * static_cast<float>(i) /
                           static_cast<float>(fft_size));
    }
    // sin^2 (Hann) overlap-adds to N / (2 hop); the FFT round trip gains N.
    gain_ = (static_cast<float>(hop) / static_cast<float>(fft_size)) * 2.0f /
        static_cast<float>(fft_size);
    pos_ = 0;
    hop_counter_ = 0;
    hops_ready_ = 0;
    hops_done_ = 0;
    return true;
  }

  void Process(const FloatFrame* in, FloatFrame* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      in_[0][pos_] = in[i].l;
      in_[1][pos_] = in[i].r;
      out[i].l = out_[0][pos_];
      out[i].r = out_[1][pos_];
      out_[0][pos_] = 0.0f;
      out_[1][pos_] = 0.0f;
      if (++pos_ >= ring_size_) pos_ = 0;
      if (++hop_counter_ >= hop_) {
        hop_counter_ = 0;
        ++hops_ready_;
      }
    }
  }

  bool Prepare() {
    if (hops_done_ == hops_ready_) return false;
    if (hops_ready_ - hops_done_ > 2) hops_done_ = hops_ready_ - 1;
    const size_t end = (hop_ * (hops_done_ + 1)) % ring_size_;
    ++hops_done_;
    float* spectrum[2] = { work_[0], work_[1] };
    const size_t start = (end + ring_size_ - fft_size_) % ring_size_;
    for (int ch = 0; ch < 2; ++ch) {
      // Unwrapped in two runs, no modulo per sample.
      const size_t first = ring_size_ - start < fft_size_
          ? ring_size_ - start : fft_size_;
      const float* src = in_[ch];
      for (size_t i = 0; i < first; ++i) temp_[i] = src[start + i] * window_[i];
      for (size_t i = first; i < fft_size_; ++i) {
        temp_[i] = src[i - first] * window_[i];
      }
      FftDirect(temp_, work_[ch], fft_size_);
      work_[ch][0] = 0.0f;
      work_[ch][fft_size_ / 2] = 0.0f;
    }
    client_->Transform(spectrum, fft_size_);
    const size_t out_start = (end + hop_) % ring_size_;
    const size_t first = ring_size_ - out_start < fft_size_
        ? ring_size_ - out_start : fft_size_;
    for (int ch = 0; ch < 2; ++ch) {
      work_[ch][0] = 0.0f;
      work_[ch][fft_size_ / 2] = 0.0f;
      FftInverse(work_[ch], temp_, fft_size_);
      float* o = out_[ch];
      const float g = gain_;
      for (size_t i = 0; i < first; ++i) {
        o[out_start + i] += temp_[i] * g * window_[i];
      }
      for (size_t i = first; i < fft_size_; ++i) {
        o[i - first] += temp_[i] * g * window_[i];
      }
    }
    return true;
  }

  size_t latency() const { return fft_size_ + hop_; }

 private:
  size_t fft_size_, hop_, ring_size_;
  StftClient* client_;
  float* in_[2];
  float* out_[2];
  float* work_[2];
  float* temp_;
  float* window_;
  float gain_;
  size_t pos_;
  size_t hop_counter_;
  volatile uint32_t hops_ready_;
  uint32_t hops_done_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_TABLE_STFT_H_
