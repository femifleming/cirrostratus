// Mode 30 — POLISH: mastering chain.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | DENSITY  | Compression amount (one macro for 3 bands): threshold       |
// |          | -12 -> -36 dBFS, ratio 1:1 -> 4:1, automatic make-up        |
// | TEXTURE  | Tape saturation: clean (CCW) -> warm, driven (CW),          |
// |          | 2x oversampled, slightly asymmetric (even harmonics)        |
// | POSITION | Tone tilt around 800 Hz: dark (CCW) / flat (noon) / bright  |
// |          | (CW), +/- 4 dB                                              |
// | SIZE     | Stereo width: mono (CCW) / as is (noon) / 2x side (CW);     |
// |          | below 120 Hz the side signal is always removed (mono bass)  |
// | PITCH    | Output trim -12 ... +12 dB (noon = 0 dB)                    |
// | FREEZE   | Bypass (A/B compare), with a 10 ms crossfade               |
// | TRIG     | Auto-gain: re-match the output loudness to the input        |
// | Options  | none                                                        |
//
// Chain: tilt -> 3-band compressor (Linkwitz-Riley 4th-order crossovers at
// 200 Hz and 3 kHz, the low band phase-matched with a 2nd-order allpass so
// the bands sum flat) -> tape saturation -> M/S width. The engine's limiter
// follows. No latency.

#ifndef MONSOON_DSP_MODES_POLISH_H_
#define MONSOON_DSP_MODES_POLISH_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class PolishMode : public Mode {
 public:
  const char* name() const { return "polish"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);

  // Diagnostics for tests: gain reduction of band b in dB.
  float gain_reduction_db(int band) const { return gr_db_[band]; }

 private:
  // Zavalishin SVF state + coefficients shared by both channels.
  struct SvfCoef {
    float g, k, a1, a2, a3;
    void Set(float hz, float q);
  };
  struct SvfState {
    float ic1, ic2;
  };
  static inline void Svf(const SvfCoef& c, SvfState* s, float x, float* lp,
                         float* bp, float* hp);

  SvfCoef c200_, c3k_, c120_;
  // Per channel: split 1 (stage 1, LP stage 2, HP stage 2), split 2 (same),
  // low-band allpass at 3 kHz.
  SvfState s1a_[2], s1l_[2], s1h_[2], s2a_[2], s2l_[2], s2h_[2], ap_[2];
  SvfState side_hp_, side_hp2_;  // 4th-order (Butterworth^2) side high-pass

  float env_[3];       // per band, linked stereo mean square
  float gr_db_[3];     // current gain reduction (dB)
  float gain_[3];      // linear gain at the end of the last block
  float tilt_lp_[2];
  float tilt_coef_;
  float up_state_[2][4], down_state_[2][4];
  float bypass_;       // 0 = processed, 1 = bypassed
  float trim_;
  float auto_gain_, auto_target_;
  float loud_in_, loud_out_;
  bool prev_trigger_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_POLISH_H_
