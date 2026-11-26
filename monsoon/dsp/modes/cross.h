// Mode 6 — CROSS: L/R spectral morph (left = modulator, right = carrier).
//
// STFT 2048 / hop 1024 (sine windows), mono synthesis. See DESIGN.md.
//
// Per-mode knobs:
//   BAL  noon = v1.0 (carrier = right input). Left: the carrier fades to
//        white noise (whispered / unvoiced, consonants come through).
//        Right: it fades to an internal buzz (band-limited saw, C3 at noon
//        of PITCH, V/OCT): a classic vocoder from one input. Right of noon
//        PITCH plays the buzz instead of transposing R (that fades out
//        over the first quarter).
//   FBK  formant emphasis: the modulator's spectral envelope is sharpened
//        (x -> x^3 / peak^2 at full; peak and energy kept), so formants
//        stand out: more talkative, then resonant, wah-like peaks.
//   RVB  the engine's FDN reverb (CROSS is mono: this gives it a stereo
//        room).

#ifndef MONSOON_DSP_MODES_CROSS_H_
#define MONSOON_DSP_MODES_CROSS_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/lean_stft.h"

namespace monsoon {

class CrossMode : public Mode, public LeanStftClient {
 public:

  const char* name() const { return "cross"; }
  bool Init(Arena* arena) __attribute__((cold));  // runs once: small
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { stft_.Prepare(); }
  bool frozen() const { return ctl_.freeze; }
  float reverb_amount(const Controls& c) const { return c.rvb; }
  // 2048, or the FFT setting: 256..2048.
  size_t native_fft_size() const { return LeanStft::kMaxSize; }

  // LeanStftClient.
  void TransformJoint(float* left, float* right);

  bool carrier_held() const { return hold_carrier_; }

 private:
  // Constant-Q box smoothing across bins (prefix sums in `prefix_`).
  void SmoothBins(const float* in, float* out, float q);

  // Run-time (the FFT setting), named as the constants they were.
  size_t kFftSize, kHop, kBins;

  LeanStft stft_;
  float* env_l_;       // time-smoothed modulator magnitudes (held on freeze)
  float* env_r_;       // time-smoothed carrier envelope (for whitening)
  float* car_mag_;     // carrier magnitudes (held on TRIG)
  float* car_freq_;    // carrier instantaneous frequency, bins (peaks)
  float* car_prev_;    // previous carrier spectrum (CMSIS order)
  float* syn_phase_;   // output synthesis phase
  float* out_mag_;     // transposed carrier
  float* out_freq_;
  float* smooth_l_;
  float* smooth_r_;
  float* prefix_;      // kBins + 1; reused for the output magnitudes

  // Per-mode knobs (kept out of line: flash is tight).
  void MixCarrier(float bal, float pitch, const FloatFrame* in,
                  FloatFrame* mixed, size_t n) __attribute__((noinline));
  void Emphasize(const float* env, float* out, float amount)
      __attribute__((noinline));

  Controls ctl_;
  volatile bool hold_carrier_;
  float buzz_phase_;   // BAL right: internal saw
  Random noise_;       // BAL left: noise carrier
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CROSS_H_
