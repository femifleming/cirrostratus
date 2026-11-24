// A lean STFT for the spectral modes (SPECTRAL, CROSS).
//
// FFT 2048, hop 1024 (2x overlap) with sine windows on analysis and
// synthesis: sin^2 + cos^2 = 1, so the overlap-add reconstructs exactly
// with no gain correction. Compared with the shared Stft (4x overlap,
// FastSin windows, ShyFFT layout conversion) this does half the FFTs, reads
// the window from a table, and works directly in CMSIS's interleaved
// layout:
//   spectrum[0] = DC, spectrum[1] = Nyquist,
//   spectrum[2k] = Re X[k], spectrum[2k+1] = Im X[k]  (k = 1 .. N/2-1),
// textbook sign convention (phase = atan2(im, re)). The inverse includes 1/N.
//
// The audio interrupt only moves samples; all FFT work is in Prepare().
// Latency: N + hop = 3072 samples (64 ms).

#ifndef MONSOON_DSP_MODES_LEAN_STFT_H_
#define MONSOON_DSP_MODES_LEAN_STFT_H_

#include "monsoon/dsp/cmsis/cmsis_fft.h"
#include "monsoon/dsp/core.h"

namespace monsoon {

class LeanStftClient {
 public:
  virtual ~LeanStftClient() { }
  // PER_CHANNEL: modify one channel's spectrum in place.
  virtual void TransformChannel(int channel, float* spectrum) {
    (void)channel; (void)spectrum;
  }
  // JOINT_MONO_OUT: read both spectra, write the (mono) result into `left`.
  virtual void TransformJoint(float* left, float* right) {
    (void)left; (void)right;
  }
};

class LeanStft {
 public:
  static const size_t kSize = 2048;
  static const size_t kHop = 1024;
  static const size_t kRing = kSize + kHop;

  enum Kind { PER_CHANNEL, JOINT_MONO_OUT };

  bool Init(Arena* arena, Kind kind, LeanStftClient* client) {
    kind_ = kind;
    client_ = client;
    const int outs = kind == PER_CHANNEL ? 2 : 1;
    const int specs = kind == PER_CHANNEL ? 1 : 2;
    for (int ch = 0; ch < 2; ++ch) {
      in_[ch] = arena->Allocate<float>(kRing);
      if (!in_[ch]) return false;
      out_[ch] = NULL;
      spec_[ch] = NULL;
    }
    for (int i = 0; i < outs; ++i) {
      out_[i] = arena->Allocate<float>(kRing);
      if (!out_[i]) return false;
    }
    for (int i = 0; i < specs; ++i) {
      spec_[i] = arena->Allocate<float>(kSize);
      if (!spec_[i]) return false;
    }
    temp_ = arena->Allocate<float>(kSize);
    window_ = arena->Allocate<float>(kSize / 2);
    if (!temp_ || !window_) return false;
    for (size_t i = 0; i < kSize / 2; ++i) {
      window_[i] = FastSin((static_cast<float>(i) + 0.5f) /
                           (2.0f * static_cast<float>(kSize)));
    }
    pos_ = 0;
    hop_counter_ = 0;
    hops_ready_ = 0;
    hops_done_ = 0;
    return true;
  }

  // Audio interrupt.
  void Process(const FloatFrame* in, FloatFrame* out, size_t n) {
    float* o0 = out_[0];
    float* o1 = out_[1] ? out_[1] : out_[0];
    for (size_t i = 0; i < n; ++i) {
      in_[0][pos_] = in[i].l;
      in_[1][pos_] = in[i].r;
      out[i].l = o0[pos_];
      out[i].r = o1[pos_];
      o0[pos_] = 0.0f;
      o1[pos_] = 0.0f;
      if (++pos_ >= kRing) pos_ = 0;
      if (++hop_counter_ >= kHop) {
        hop_counter_ = 0;
        ++hops_ready_;
      }
    }
  }

  // Main loop. Returns true if a frame was processed.
  bool Prepare() {
    if (hops_done_ == hops_ready_) return false;
    if (hops_ready_ - hops_done_ > 1) hops_done_ = hops_ready_ - 1;
    const size_t end = (kHop * (hops_done_ + 1)) % kRing;
    ++hops_done_;
    const size_t start = (end + kRing - kSize) % kRing;
    const size_t out_start = (end + kHop) % kRing;

    if (kind_ == PER_CHANNEL) {
      for (int ch = 0; ch < 2; ++ch) {
        Analyze(in_[ch], start, spec_[0]);
        client_->TransformChannel(ch, spec_[0]);
        Synthesize(spec_[0], out_[ch], out_start);
      }
    } else {
      Analyze(in_[0], start, spec_[0]);
      Analyze(in_[1], start, spec_[1]);
      client_->TransformJoint(spec_[0], spec_[1]);
      Synthesize(spec_[0], out_[0], out_start);
    }
    return true;
  }

  size_t latency() const { return kSize + kHop; }

 private:
  void Analyze(const float* ring, size_t start, float* spectrum) {
    const size_t half = kSize / 2;
    for (size_t i = 0; i < half; ++i) {
      size_t a = start + i;
      if (a >= kRing) a -= kRing;
      size_t b = start + kSize - 1 - i;
      if (b >= kRing) b -= kRing;
      temp_[i] = ring[a] * window_[i];
      temp_[kSize - 1 - i] = ring[b] * window_[i];
    }
    monsoon_cmsis_rfft(temp_, spectrum, kSize, 0);
    spectrum[0] = 0.0f;
    spectrum[1] = 0.0f;
  }

  void Synthesize(float* spectrum, float* ring, size_t start) {
    spectrum[0] = 0.0f;
    spectrum[1] = 0.0f;
    monsoon_cmsis_rfft(spectrum, temp_, kSize, 1);
    const size_t half = kSize / 2;
    for (size_t i = 0; i < half; ++i) {
      size_t a = start + i;
      if (a >= kRing) a -= kRing;
      size_t b = start + kSize - 1 - i;
      if (b >= kRing) b -= kRing;
      ring[a] += temp_[i] * window_[i];
      ring[b] += temp_[kSize - 1 - i] * window_[i];
    }
  }

  Kind kind_;
  LeanStftClient* client_;
  float* in_[2];
  float* out_[2];
  float* spec_[2];
  float* temp_;
  float* window_;
  size_t pos_;
  size_t hop_counter_;
  volatile uint32_t hops_ready_;
  uint32_t hops_done_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_LEAN_STFT_H_
