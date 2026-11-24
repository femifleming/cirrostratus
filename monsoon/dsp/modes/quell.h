// Mode 16 — QUELL: resonance suppressor (spectral dynamic EQ).
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | Frequency focus: where detection listens hardest, 100 Hz    |
// |          | (CCW) -> 16 kHz (CW), a 2.5-octave-wide bell                |
// | SIZE     | Selectivity: only sharp, narrow ringing (CCW, ~1/12 octave) |
// |          | -> broad, honky areas (CW, ~1 octave)                       |
// | DENSITY  | Depth: maximum cut 0 -> 24 dB (fully CCW = transparent)     |
// | TEXTURE  | Speed: slow and smooth (CCW, ~50/400 ms) -> fast (CW,       |
// |          | ~3/40 ms attack/release)                                    |
// | PITCH    | Output trim -12 ... +12 dB (noon = 0 dB)                    |
// | FREEZE   | Hold the current cut curve (becomes a static EQ)            |
// | TRIG     | Toggle "delta": hear only what is being removed             |
// | Options  | none                                                        |
//
// STFT 1024 / hop 256 (4x overlap), stereo-linked: one cut curve from the
// summed power of both channels is applied to both, so the image doesn't
// shift. A bin's "excess" is how far it stands above a constant-Q average
// of its neighbours (in log magnitude); excess beyond 3 dB is cut.
// Latency 1280 samples (27 ms).

#ifndef MONSOON_DSP_MODES_QUELL_H_
#define MONSOON_DSP_MODES_QUELL_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/modes/table_stft.h"

namespace monsoon {

class QuellMode : public Mode, public StftClient {
 public:
  static const size_t kFftSize = 1024;
  static const size_t kHop = 256;
  static const size_t kBins = kFftSize / 2;

  const char* name() const { return "quell"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare() { while (stft_.Prepare()) { } }
  void Transform(float* spectrum[2], size_t fft_size);

  // Diagnostics for tests: current cut in dB at a frequency.
  float cut_db(float hz) const;
  bool delta() const { return delta_; }

 private:
  TableStft stft_;
  float* log_mag_;     // kBins, log2 of magnitude
  float* prefix_;      // kBins + 1, prefix sums of log_mag_
  float* cut_;         // kBins, current cut in log2 units (1 = 6 dB)
  float* weight_;      // kBins, focus weighting
  float focus_built_;

  // Controls handed from the ISR to Prepare().
  volatile float position_, size_, density_, texture_;
  volatile bool freeze_;
  bool delta_;
  bool prev_trigger_;
  float trim_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_QUELL_H_
