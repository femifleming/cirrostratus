// Mode 20 — PUTZ: spectral cleaner (denoise + de-hum).
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | DENSITY  | Reduction amount: 0 = off -> up to 30 dB less noise (also   |
// |          | raises the over-subtraction)                                |
// | SIZE     | Smoothing over time and frequency (CW = fewer "musical      |
// |          | noise" twitters, a little softer)                           |
// | TEXTURE  | Transient protection: CW lets attacks through untouched     |
// | POSITION | Hum comb depth (0 = off, 1 = full notches on 8 harmonics)   |
// | PITCH    | Hum fundamental, noon = 50 Hz (option 0) / 60 Hz (option 1),|
// |          | +/- one octave                                              |
// | FREEZE   | Hold to learn the noise: play only the noise while FREEZE   |
// |          | is held. Before anything is learned the noise floor is      |
// |          | tracked automatically (minimum statistics).                 |
// | TRIG     | Toggle "listen": hear only what the spectral cleaner removes|
// | Options  | 0 = 50 Hz mains, 1 = 60 Hz mains                            |
//
// Per-mode knobs:
//   BAL  tilt of the reduction, noon = v1.0 (even). Left: the lows are
//        cleaned harder (twice at full) and the highs left alone; right:
//        the other way round (hiss only, the bass untouched). Pivot
//        ~1.5 kHz.
//   FBK  de-crackle on the input: whatever a ~2.5 kHz low-pass does not
//        predict is limited to a few times its own running level
//        (28x -> 3x), so clicks and vinyl crackle are flattened while
//        steady sound passes.
//   RVB  de-reverb: every bin's decay is made steeper than it really is
//        (up to 3x faster in dB at full), which pulls down the room tail
//        and keeps attacks, steady notes and slow natural decays (it acts
//        on energy falling faster than 60 dB in ~4 s; at most -24 dB).
//        Works with DENSITY at 0 too.
//
// STFT 1024 / hop 512 (2x overlap, sine windows), stereo-linked Wiener-
// style gain with a floor (2x keeps the CPU in budget next to the 16 hum
// notches; the gain smoothing hides the coarser time grid).
// The hum comb runs in the time domain after the STFT. Latency 1536.

#ifndef MONSOON_DSP_MODES_PUTZ_H_
#define MONSOON_DSP_MODES_PUTZ_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/knob_fx.h"
#include "monsoon/dsp/modes/table_stft.h"

namespace monsoon {

class PutzMode : public Mode, public StftClient {
 public:
  static const int kHarmonics = 8;

  const char* name() const { return "putz"; }
  // 1024 (hop N/2), or the FFT setting: 256..1024 (2048 needs ~124 KB of
  // the ~128 KB arena: it doesn't fit).
  size_t native_fft_size() const { return 1024; }
  size_t max_fft_size() const { return 1024; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { while (stft_.Prepare()) { } }
  void Transform(float* spectrum[2], size_t fft_size);
  uint8_t num_options() const { return 2; }

  bool learned() const { return learned_; }
  bool listening() const { return listen_; }

 private:
  struct Notch {
    float b0, b1, a1, a2;   // b2 == b0 for a notch
    float x1[2], x2[2], y1[2], y2[2];
  };
  void UpdateNotches(float f0);
  float KnobPower(size_t k, float p, float alpha, float gmin2) KNOB_FX;
  const FloatFrame* Knobs(const Controls& c, const FloatFrame* in,
                          FloatFrame* clean, size_t n) KNOB_FX;

  TableStft stft_;
  float* noise_;      // estimated noise power per bin
  float* smooth_;     // smoothed power (minimum statistics)
  float* learn_sum_;  // power summed while learning
  float* gain_;       // smoothed gain per bin
  float* win_min_;    // minimum of smooth_ in the current ~1 s window
  float* prev_min_;   // ... and in the previous one
  // Run-time (the FFT setting), named as the constants they were.
  size_t kFftSize, kHop, kBins;
  // Per-frame rates, so the times are the same at every FFT size
  // (hop / 512 = 1 at the native size: exactly the old numbers).
  float frame_ratio_;
  float smooth_a_;       // power smoothing (0.27)
  float tail_keep_;      // RVB tail follower (0.963)
  float protect_keep_;   // transient gate release (0.5)
  uint32_t win_len_;     // minimum-statistics window, frames (94, ~1 s)
  uint32_t win_frames_;
  uint32_t learn_frames_;
  bool learned_;
  bool learning_;
  float prev_energy_;
  float protect_;

  volatile float density_, size_, texture_;
  volatile bool freeze_;
  bool listen_;
  bool prev_trigger_;
  volatile float bal_, rvb_;
  float* tail_;       // RVB: decaying peak of smooth_ per bin
  float click_[4];    // FBK: low-pass and level per side

  Notch notch_[kHarmonics];
  float notch_f0_;
  float comb_depth_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_PUTZ_H_
