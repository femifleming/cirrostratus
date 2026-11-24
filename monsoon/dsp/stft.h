// Short-time Fourier transform framework.
//
// The audio interrupt only moves samples in and out of two ring buffers;
// all FFT work happens in Prepare(), which the firmware calls from its main
// loop (exactly like Clouds' phase vocoder). Latency is fft_size + hop.
//
// Spectra use ShyFFT's real layout: re[k] = x[k], im[k] = x[N/2 + k],
// for k in 1 .. N/2-1. Bin 0 (DC) and N/2 are cleared by the framework.
// NOTE: ShyFFT's imaginary part has the opposite sign to the textbook DFT
// (phase = atan2(-im, re)); a round trip has a gain of N.

#ifndef MONSOON_DSP_STFT_H_
#define MONSOON_DSP_STFT_H_

#include "monsoon/dsp/core.h"
#include "stmlib/fft/shy_fft.h"

namespace monsoon {

const size_t kMaxFftSize = 2048;
typedef stmlib::ShyFFT<float, kMaxFftSize, stmlib::RotationPhasor> Fft;

// One shared FFT engine (twiddles only, a few hundred bytes).
Fft& SharedFft();

inline size_t FftPasses(size_t n) {
  size_t p = 0;
  while ((size_t(1) << p) < n) ++p;
  return p;
}
// Real FFT of any power-of-two size up to kMaxFftSize, in ShyFFT layout.
// Implemented with CMSIS-DSP in fft_cmsis.cc (256..2048), ShyFFT otherwise.
void FftDirect(float* in, float* out, size_t n);
void FftInverse(float* in, float* out, size_t n);

// The original ShyFFT path, kept for the equivalence test.
inline void ShyFftDirect(float* in, float* out, size_t n) {
  if (n == kMaxFftSize) SharedFft().Direct(in, out);
  else SharedFft().Direct(in, out, FftPasses(n));
}
inline void ShyFftInverse(float* in, float* out, size_t n) {
  if (n == kMaxFftSize) SharedFft().Inverse(in, out);
  else SharedFft().Inverse(in, out, FftPasses(n));
}

class StftClient {
 public:
  virtual ~StftClient() { }
  // Fill `frame` (fft_size samples, unwindowed) for this channel. Default:
  // most recent input. Override to analyse something else (a buffer).
  virtual bool FillFrame(int channel, float* frame, size_t fft_size) {
    (void)channel; (void)frame; (void)fft_size;
    return false;
  }
  // Modify both spectra in place. spectrum[ch] has fft_size floats.
  virtual void Transform(float* spectrum[2], size_t fft_size) = 0;
};

class Stft {
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
    if (!temp_) return false;
    // Hann^2 (sqrt-Hann on both sides) at hop N/4 overlaps to 2; ShyFFT's
    // round trip has a gain of N... measured by the harness and folded here.
    gain_ = (static_cast<float>(hop) / static_cast<float>(fft_size)) * 2.0f /
        static_cast<float>(fft_size);
    Reset();
    return true;
  }

  void Reset() {
    for (int ch = 0; ch < 2; ++ch) {
      memset(in_[ch], 0, ring_size_ * sizeof(float));
      memset(out_[ch], 0, ring_size_ * sizeof(float));
    }
    pos_ = 0;
    hop_counter_ = 0;
    hops_ready_ = 0;
    hops_done_ = 0;
    // Pre-roll: frames are written fft_size + hop ahead of the read point.
    frame_end_ = 0;
  }

  // Audio interrupt. Reads n frames of input, writes n frames of output.
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

  // Main loop. Returns true if a frame was processed.
  bool Prepare() {
    if (hops_done_ == hops_ready_) return false;
    // Fell behind by more than a frame: skip ahead (glitch, but stay sane).
    if (hops_ready_ - hops_done_ > 2) hops_done_ = hops_ready_ - 1;
    // Analysis frame ends at the sample at index `end` (exclusive).
    size_t end = (hop_ * (hops_done_ + 1)) % ring_size_;
    ++hops_done_;

    float* spectrum[2] = { work_[0], work_[1] };
    const float inv_n = 1.0f / static_cast<float>(fft_size_);
    for (int ch = 0; ch < 2; ++ch) {
      if (!client_->FillFrame(ch, temp_, fft_size_)) {
        size_t start = (end + ring_size_ - fft_size_) % ring_size_;
        for (size_t i = 0; i < fft_size_; ++i) {
          size_t j = start + i;
          if (j >= ring_size_) j -= ring_size_;
          temp_[i] = in_[ch][j];
        }
      }
      for (size_t i = 0; i < fft_size_; ++i) {
        temp_[i] *= FastSin(0.5f * static_cast<float>(i) * inv_n);
      }
      FftDirect(temp_, work_[ch], fft_size_);
      work_[ch][0] = 0.0f;
      work_[ch][fft_size_ / 2] = 0.0f;
    }

    client_->Transform(spectrum, fft_size_);

    // Synthesis: write so the first sample lands hop samples after `end`.
    size_t start = (end + hop_) % ring_size_;
    for (int ch = 0; ch < 2; ++ch) {
      work_[ch][0] = 0.0f;
      work_[ch][fft_size_ / 2] = 0.0f;
      FftInverse(work_[ch], temp_, fft_size_);
      float* o = out_[ch];
      for (size_t i = 0; i < fft_size_; ++i) {
        size_t j = start + i;
        if (j >= ring_size_) j -= ring_size_;
        o[j] += temp_[i] * gain_ * FastSin(0.5f * static_cast<float>(i) * inv_n);
      }
    }
    return true;
  }

  size_t fft_size() const { return fft_size_; }
  size_t hop() const { return hop_; }
  size_t latency() const { return fft_size_ + hop_; }

 private:
  size_t fft_size_, hop_, ring_size_;
  StftClient* client_;
  float* in_[2];
  float* out_[2];
  float* work_[2];
  float* temp_;
  float gain_;
  size_t pos_;
  size_t hop_counter_;
  volatile uint32_t hops_ready_;
  uint32_t hops_done_;
  size_t frame_end_;
};

// Spectrum helpers for ShyFFT layout.
inline float* Re(float* s) { return s; }
inline float* Im(float* s, size_t n) { return s + n / 2; }

}  // namespace monsoon

#endif  // MONSOON_DSP_STFT_H_
