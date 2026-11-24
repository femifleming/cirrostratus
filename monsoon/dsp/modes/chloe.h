// CHLOE: a port of the Chloaelis channel strip (~/chloe, Source/DSP.h) to the
// Monsoon. Two strips, left and right: multimode filter > 12AX7 tube >
// delay engine (PT2399 / bucket brigade / tape / Karplus-Strong string),
// with the Lorenz attractor pulling the two cutoffs apart.
//
// Differences from the plug-in, for a 168 MHz Cortex-M4:
//  - the triode curve is a lookup table and the tube runs 2x oversampled
//    (the plug-in uses 4x); only the 12AX7 type,
//  - one delay line per side (time changes always glide, "slew" mode),
//  - the tape loop is 16-bit and at most 0.6 s long, with a fixed amount of
//    wear (0.45 s per side); PT2399 bits are packed 8 per byte,
//  - no poly taps / flock.
//
// Per-mode knobs (from the plug-in's shared controls):
//   BAL  the attractor (speed and depth): noon = v1.0 (+/-9 semitones,
//        slow, LP <-> BP). Left: calmer and shallower until it stops (the
//        two cutoffs together, the filter halfway LP/BP, 1/4 speed).
//        Right: up to 8x faster and +/-3 octaves, the filter wandering
//        LP <-> BP <-> HP.
//   FBK  master feedback, crossed like the plug-in's: the left output
//        feeds the right input and the right the left (20 Hz high-pass,
//        tanh, up to 0.95). With regen it ping-pongs and can sing.
//   RVB  diffusion in the regen loop (after the plug-in's poly cloud): four
//        allpasses per side smear every repeat a little more, so echoes
//        blur into a cloud. String engine: it diffuses the strings' output
//        (a soundboard haze).

#ifndef MONSOON_DSP_MODES_CHLOE_H_
#define MONSOON_DSP_MODES_CHLOE_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {
namespace chloe {

enum Engine { ENGINE_PT2399, ENGINE_BBD, ENGINE_TAPE, ENGINE_STRING };

struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void Lowpass(float fc, float q);
  void Highpass(float fc, float q);
  void Peak(float fc, float q, float db);
  inline float Process(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void Reset() { z1 = z2 = 0.0f; }
};

struct DcBlock {
  float r = 0.999f, x1 = 0, y1 = 0;
  void Init(float fc) { r = 1.0f - kTwoPi * fc / kSampleRate; x1 = y1 = 0; }
  inline float Process(float x) {
    const float y = x - x1 + r * y1;
    x1 = x;
    y1 = y;
    return y;
  }
};

// Zavalishin SVF with a saturating band integrator, LP > BP > HP > notch.
struct MultiModeFilter {
  float ic1 = 0, ic2 = 0;
  float Process(float x, float g, float k, float morph);
};

struct TriodeStage {
  float bias = 0, att = 0, rel = 0;
  void Init(float osr);
  inline float Process(float vg);
};

// Two 12AX7 stages at 2x the sample rate.
struct Tube {
  TriodeStage v1, v2;
  DcBlock inter, dc;
  float miller = 0, out1 = 0, post_a = 0;
  float up_state[4];
  float down_state[4];
  float onmix = 0;
  void Init();
  float Process(float x, float drive, float gain, float makeup, float miller_a,
                float c2, bool on);
};

struct Pt2399Line {
  static const int kBits = 44000;
  uint8_t* mem;  // kBits / 8 bytes
  int pos;
  float bit_rate, target_rate, bit_phase, prev_in, held;
  float enc_est, enc_step, dec_est, dec_step;
  unsigned enc_hist, dec_hist;
  float leak, beta, d_min, d_max, clock_a, coeff_rate;
  Biquad in_lp, out_lp1, out_lp2;
  DcBlock dc;
  Random rng;
  bool Init(Arena* arena, uint32_t seed);
  void SetTime(float sec) { target_rate = kBits / Clamp(sec, 0.02f, 1.6f); }
  void UpdateCoeffs();
  // Recomputes only when the clock moved by more than 0.3 %.
  void MaybeUpdateCoeffs() {
    if (fabsf(bit_rate - coeff_rate) > 0.003f * coeff_rate) UpdateCoeffs();
  }
  float Process(float x, bool freeze);
};

struct BbdLine {
  static const int kStages = 4096;
  float* bucket;
  int idx;
  float clk, target_clk, clk_a, tick_phase, prev_aa, held, cti, parity;
  float comp_env, exp_env, c_att, c_rel, e_att, e_rel;
  Biquad aa1, aa2, rc1, rc2;
  DcBlock dc;
  Random rng;
  bool Init(Arena* arena, uint32_t seed);
  void SetTime(float sec) { target_clk = kStages / Clamp(sec, 0.02f, 1.6f); }
  float Process(float x, bool freeze);
};

struct TapeLine {
  static const int kMaxFrames = 21600;  // 0.45 s per side
  int16_t* buf;
  int size, w;
  float d_a, d_s, d_target, slew_a;
  bool frozen;
  bool primed;  // first SetTime jumps (a new tape starts at speed)
  float loop_start, loop_len, loop_pos;
  float wow_t, wow1, wow2, wow_a, wow_timer, flut_ph, flut_ph2, flut_inc,
      flut_inc2;
  float pre, pre_a, gap1, gap2, deemph, gap_a, hiss_lp, hiss_hp, hiss_a,
      hiss_ha;
  Biquad bump;
  DcBlock dc;
  Random rng;
  bool Init(Arena* arena, uint32_t seed);
  void SetTime(float sec);
  inline float ReadAt(float p) const;
  float Process(float in, bool freeze);
};

// Four Schroeder allpasses in series (RVB).
struct Diffuser {
  static const int kStages = 4;
  float* buf;
  int len[kStages], pos[kStages];
  bool Init(Arena* arena, int side);
  void Clear();
  inline float Process(float x);
};

struct KsLine {
  static const int kSize = 4336;  // 90 ms
  float* buf;
  int w;
  float d_a, d_target, slew_a, lp, dc_lead;
  float msq, msq_a, pk;
  DcBlock dc;
  bool Init(Arena* arena);
  void SetPeriod(float sec);
  inline float ReadAt(float d) const;
  float Feedback(float f, float regen);
  float Process(float in, bool freeze, float damp);
};

}  // namespace chloe

class ChloeMode : public Mode {
 public:
  const char* name() const { return "chloe"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  // 0-3: PT2399, BBD, tape, string with filter > tube > delay;
  // 4-7: the same with delay > tube > filter.
  uint8_t num_options() const { return 8; }
  bool option_requires_init() const { return true; }
  // Test hook: the current cutoff of one side (Hz).
  float cutoff_hz(int i) const { return atanf(g_[i]) * kSampleRate / kPi; }

 private:
  struct Side {
    chloe::MultiModeFilter filter;
    chloe::Tube tube;
    chloe::Pt2399Line* pt;
    chloe::BbdLine* bbd;
    chloe::TapeLine* tape;
    chloe::KsLine* ks;
    chloe::Biquad fb_hp;
    float fb;
    chloe::Diffuser* diff;
    float xfb, xfb_x1, xfb_y1;   // FBK: this side's output, high-passed
  };
  float RunDelay(Side* s, float x, float time, float regen, bool freeze,
                 float ks_damp, float diffuse);

  Side side_[2];
  int engine_;
  bool delay_first_;
  // Lorenz attractor (control rate).
  float lx_, ly_, lz_;
  Random rng_;
  float g_[2], cut_semis_[2];
  float drive_, morph_;
  float random_freeze_timer_;
  float pluck_timer_;
  bool was_trigger_;
  float diffuse_;      // RVB, smoothed
  float cross_;        // FBK, ramped
  bool diff_live_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CHLOE_H_
