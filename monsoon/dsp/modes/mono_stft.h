// Lean STFT for modes that analyse one channel (the mid, L+R) and
// resynthesise one or two channels. Saves an FFT per hop compared with the
// stereo Stft. Same scheduling as Stft: samples move in Process() (audio
// interrupt), FFTs happen in Prepare() (main loop). Sine (sqrt-Hann)
// windows on both sides; any hop of N/2 or N/4. Latency = fft_size + hop.
// Used by SPECDELAY and MIRROR.

#ifndef MONSOON_DSP_MODES_MONO_STFT_H_
#define MONSOON_DSP_MODES_MONO_STFT_H_

#include "monsoon/dsp/core.h"
#include "monsoon/dsp/stft.h"

namespace monsoon {

class MonoStftClient {
 public:
  virtual ~MonoStftClient() { }
  // `in` is the analysis spectrum (ShyFFT layout, may be modified).
  // Write the output spectrum/spectra into out[0] (and out[1] if stereo).
  virtual void Transform(float* in, float* out[2], size_t fft_size) = 0;
};

class MonoStft {
 public:
  bool Init(Arena* arena, size_t fft_size, size_t hop, int out_channels,
            MonoStftClient* client) {
    fft_size_ = fft_size;
    hop_ = hop;
    outs_ = out_channels;
    client_ = client;
    ring_size_ = fft_size + 2 * hop;
    in_ = arena->Allocate<float>(ring_size_);
    work_in_ = arena->Allocate<float>(fft_size);
    temp_ = arena->Allocate<float>(fft_size);
    if (!in_ || !work_in_ || !temp_) return false;
    for (int ch = 0; ch < 2; ++ch) {
      out_[ch] = NULL;
      work_out_[ch] = NULL;
    }
    for (int ch = 0; ch < outs_; ++ch) {
      out_[ch] = arena->Allocate<float>(ring_size_);
      work_out_[ch] = arena->Allocate<float>(fft_size);
      if (!out_[ch] || !work_out_[ch]) return false;
    }
    // sin x sin = Hann; Hann overlaps to N/(2 hop); FFT round trip gain N.
    gain_ = (static_cast<float>(hop) / static_cast<float>(fft_size)) * 2.0f /
        static_cast<float>(fft_size);
    pos_ = 0;
    hop_counter_ = 0;
    hops_ready_ = 0;
    hops_done_ = 0;
    return true;
  }

  // Audio interrupt: mid in, one or two channels out (mono: L = R).
  void Process(const FloatFrame* in, FloatFrame* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      in_[pos_] = 0.5f * (in[i].l + in[i].r);
      float l = out_[0][pos_];
      out_[0][pos_] = 0.0f;
      float r = l;
      if (outs_ == 2) {
        r = out_[1][pos_];
        out_[1][pos_] = 0.0f;
      }
      out[i].l = l;
      out[i].r = r;
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
    const float inv_n = 1.0f / static_cast<float>(fft_size_);

    size_t start = (end + ring_size_ - fft_size_) % ring_size_;
    for (size_t i = 0; i < fft_size_; ++i) {
      size_t j = start + i;
      if (j >= ring_size_) j -= ring_size_;
      temp_[i] = in_[j] * FastSin(0.5f * static_cast<float>(i) * inv_n);
    }
    FftDirect(temp_, work_in_, fft_size_);
    work_in_[0] = 0.0f;
    work_in_[fft_size_ / 2] = 0.0f;

    float* outs[2] = { work_out_[0], work_out_[1] };
    client_->Transform(work_in_, outs, fft_size_);

    start = (end + hop_) % ring_size_;
    for (int ch = 0; ch < outs_; ++ch) {
      work_out_[ch][0] = 0.0f;
      work_out_[ch][fft_size_ / 2] = 0.0f;
      FftInverse(work_out_[ch], temp_, fft_size_);
      float* o = out_[ch];
      for (size_t i = 0; i < fft_size_; ++i) {
        size_t j = start + i;
        if (j >= ring_size_) j -= ring_size_;
        o[j] += temp_[i] * gain_ *
            FastSin(0.5f * static_cast<float>(i) * inv_n);
      }
    }
    return true;
  }

  size_t latency() const { return fft_size_ + hop_; }

 private:
  size_t fft_size_, hop_, ring_size_;
  int outs_;
  MonoStftClient* client_;
  float* in_;
  float* out_[2];
  float* work_in_;
  float* work_out_[2];
  float* temp_;
  float gain_;
  size_t pos_, hop_counter_;
  volatile uint32_t hops_ready_;
  uint32_t hops_done_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_MONO_STFT_H_
