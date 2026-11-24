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
// Per-mode knobs:
//   BAL  where the compressor leans: noon = v1.0 (one threshold for all
//        three bands). Left: the low band's threshold drops (up to 9 dB)
//        and the high band's rises: tighter, denser bass. Right: the
//        opposite: the highs are tamed (de-harsh), the bass breathes.
//        Needs some DENSITY.
//   FBK  parallel ("New York") compression: a smashed copy (-30 dBFS,
//        10:1, 2 ms / 120 ms, +15 dB) blended under the chain, up to
//        -6 dB. Quiet detail comes up ~+11 dB, peaks stay (~+1 dB).
//   RVB  air: the band above 3 kHz is rectified (even harmonics) and
//        high-passed at 6 kHz, then added: sparkle that wasn't there,
//        not just EQ. Follows the material's level.
//
// Chain: tilt -> 3-band compressor (Linkwitz-Riley 4th-order crossovers at
// 200 Hz and 3 kHz, the low band phase-matched with a 2nd-order allpass so
// the bands sum flat) -> parallel compression (FBK) -> tape saturation ->
// air (RVB) -> M/S width. The engine's limiter follows. No latency.

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

  SvfCoef c200_, c3k_, c120_, c6k_;
  // Per channel: split 1 (stage 1, LP stage 2, HP stage 2), split 2 (same),
  // low-band allpass at 3 kHz.
  SvfState s1a_[2], s1l_[2], s1h_[2], s2a_[2], s2l_[2], s2h_[2], ap_[2];
  SvfState side_hp_, side_hp2_;  // 4th-order (Butterworth^2) side high-pass
  SvfState air_hp_[2];           // RVB: high-pass after the rectifier

  // Per band (band 3 = FBK's parallel compressor on the sum).
  float env_[4];       // linked stereo mean square
  float gr_db_[4];     // current gain reduction (dB)
  float gain_[4];      // linear gain at the end of the last block
  float ny_mix_;       // FBK blend (ramped)
  float air_mix_;      // RVB amount (ramped)
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
