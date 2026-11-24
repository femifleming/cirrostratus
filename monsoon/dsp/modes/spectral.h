// Mode 2 — SPECTRAL: spectral freeze / resynthesis.
//
// STFT 2048 / hop 512, stereo, phase-vocoder resynthesis. See DESIGN.md.

#ifndef MONSOON_DSP_MODES_SPECTRAL_H_
#define MONSOON_DSP_MODES_SPECTRAL_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/stft.h"

namespace monsoon {

class SpectralMode : public Mode, public StftClient {
 public:
  static const size_t kFftSize = 2048;
  static const size_t kHop = 512;
  static const size_t kBins = kFftSize / 2;  // index 1 .. kBins-1 used

  const char* name() const { return "spectral"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { stft_.Prepare(); }

  size_t GetPersistentData(PersistentChunk* chunks);
  bool LoadPersistentData(const uint8_t* data, size_t size);
  void SetMorphSource(const uint8_t* data, size_t size);
  bool supports_morph() const { return true; }
  bool frozen() const { return frozen_; }

  // StftClient.
  void Transform(float* spectrum[2], size_t fft_size);

 private:
  void TransformChannel(int ch, float* spectrum);

  Stft stft_;
  float* mag_[2];        // smoothed / frozen magnitudes
  float* freq_[2];       // instantaneous frequency, in bins
  float* an_phase_[2];   // last analysis phase (cycles)
  float* syn_phase_[2];  // running synthesis phase (cycles)
  float* out_mag_;       // scratch
  float* out_freq_;      // scratch (magnitude-weighted frequency)
  const float* morph_mag_[2];
  const float* morph_freq_[2];

  Controls ctl_;
  bool prev_freeze_;
  bool hold_loaded_;     // a loaded spectrum is held until FREEZE changes
  volatile bool frozen_;
  volatile bool grab_;
  Random rng_;
  uint32_t keep_[kBins / 32];  // bins kept by partial thinning
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SPECTRAL_H_
