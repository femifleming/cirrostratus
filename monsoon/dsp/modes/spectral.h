// Mode 2 — SPECTRAL: spectral freeze / resynthesis.
//
// STFT 2048 / hop 1024 (sine windows), stereo, peak-locked phase vocoder.
// See DESIGN.md.
//
// Per-mode knobs:
//   BAL  noon = v1.0. Linear frequency shift (Hz, not a ratio: harmonic
//        sounds turn inharmonic): left down, right up, exponential up to
//        1 kHz (a few Hz near noon).
//   FBK  regeneration: the last output spectrum (before tilt) comes back as
//        a floor under the next frame (per-bin max), held 0.1 -> 8 s. Alone
//        it sustains partials; with BAL or PITCH every pass moves them
//        again, so they spiral up or down.
//   RVB  phase diffusion: random phases blended in per bin (coherent ->
//        whispered wash with the same colours).

#ifndef MONSOON_DSP_MODES_SPECTRAL_H_
#define MONSOON_DSP_MODES_SPECTRAL_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/lean_stft.h"

namespace monsoon {

class SpectralMode : public Mode, public LeanStftClient {
 public:
  static const size_t kFftSize = LeanStft::kSize;
  static const size_t kHop = LeanStft::kHop;
  static const size_t kBins = kFftSize / 2;  // index 1 .. kBins-1 used

  const char* name() const { return "spectral"; }
  bool Init(Arena* arena) __attribute__((cold));  // runs once: small
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { stft_.Prepare(); }

  size_t GetPersistentData(PersistentChunk* chunks);
  bool LoadPersistentData(const uint8_t* data, size_t size);
  void SetMorphSource(const uint8_t* data, size_t size);
  bool supports_morph() const { return true; }
  bool frozen() const { return frozen_; }

  // LeanStftClient.
  void TransformChannel(int ch, float* spectrum);

 private:
  void UpdateTilt(float slope);
  // Per-mode knobs (kept out of line: flash is tight).
  float Regenerate(int ch, const float* src, float fbk)
      __attribute__((noinline));
  void Remember(int ch, bool tilt, float energy) __attribute__((noinline));
  void Diffuse(float* spectrum, float amount) __attribute__((noinline));

  LeanStft stft_;
  float* mag_[2];        // smoothed / frozen magnitudes
  float* freq_[2];       // instantaneous frequency in bins (peaks; k elsewhere)
  float* prev_[2];       // previous analysis spectrum (CMSIS order)
  float* syn_phase_[2];  // running synthesis phase (cycles)
  float* raw_;           // scratch: this frame's magnitudes
  float* out_mag_;       // scratch
  float* out_freq_;      // scratch (magnitude-weighted frequency)
  float* tilt_;          // per-bin tilt gain, rebuilt when TEXTURE moves
  uint16_t* fb_[2];      // FBK: last output magnitudes (before tilt) ...
  float fb_scale_[2];    // ... as fractions of this scale
  float tilt_slope_;
  const float* morph_mag_[2];
  const float* morph_freq_[2];

  Controls ctl_;
  Controls frame_ctl_;   // controls latched for the current frame
  bool prev_freeze_;
  bool hold_loaded_;     // a loaded spectrum is held until FREEZE changes
  volatile bool frozen_;
  volatile bool grab_;
  bool frame_hold_;
  bool frame_grab_;
  Random rng_;
  uint32_t keep_[kBins / 32];  // bins kept by partial thinning
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SPECTRAL_H_
