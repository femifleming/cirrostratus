// Mode 18 - TANTRI: tabla + tanpura, an M4-lite port of ~/tantri.
//
// Controls
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | stroke for TRIG, 8 zones: Na, Tin, Tun, Te (dayan),          |
// |          | Ge, Ke (bayan), Dha (Na+Ge), Dhin (Tin+Ge)                    |
// | SIZE     | bayan palm pressure: bends the bayan up to +55 %             |
// |          | (Ge / Dha / Dhin also add their own rising-then-falling ghe)  |
// | DENSITY  | tanpura: 0 = silent, then louder and faster plucking          |
// |          | (cycle Pa Sa Sa sa at 30 -> 90 plucks per minute)             |
// | TEXTURE  | jawari: 0 = plain string, 1 = full curved-bridge buzz         |
// | PITCH    | Sa, V/oct (C3 = 130.81 Hz at noon). Dayan = upper Sa,         |
// |          | bayan = 0.75 x Sa, tanpura Pa Sa Sa sa (Sa clamped 55-523 Hz) |
// | FREEZE   | tanpura drone sustains forever at its current level (loss-  |
// |          | less loop + a slow level servo, like Tantri's bowed drone);   |
// |          | auto-plucking stops, input no longer reaches the strings;    |
// |          | drums keep working                                           |
// | TRIG     | play the stroke chosen by POSITION                           |
// | input    | excites the strings (sympathetic) and both drum heads        |
// | options  | none                                                         |
//
// What is ported from Tantri:
//  - tabla: Tantri's modal membranes with Raman's loaded (harmonic) dayan
//    ratios and the bayan's stretched ones, per-group stroke excitation,
//    damping left on the head by closed strokes (Na, Te, Ke), raised-cosine
//    contact pulses, a band-passed noise "snap", and the ghe glide. The
//    Bessel radial shapes for each stroke are precomputed (constants).
//  - tanpura: Tantri's passive curved-bridge termination (a chain of three
//    normalised lattice allpasses whose coefficient follows how hard the
//    string presses into the bridge), with the same average-wrap pitch
//    compensation and loss/T60 handling, plus a 4-mode gourd body.
// Left out for the M4: 2x oversampling and the two-rail waveguide (one
// delay loop per string), the second polarisation, the distributed
// barrier, bowed drone mode, and 6 of the 14 tabla bols.

#ifndef MONSOON_DSP_MODES_TANTRI_H_
#define MONSOON_DSP_MODES_TANTRI_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class TantriMode : public Mode {
 public:
  static const int kDayanModes = 14;
  static const int kBayanModes = 8;
  static const int kStrings = 4;
  static const size_t kLineSize = 2048;
  static const size_t kLineMask = kLineSize - 1;

  enum Stroke { NA, TIN, TUN, TE, GE, KE, DHA, DHIN, NUM_STROKES };

  const char* name() const { return "tantri"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

  // Test hooks.
  void Play(int stroke);
  float string_hz(int i) const { return string_hz_[i]; }
  float dayan_hz() const { return dayan_f0_; }
  float bayan_hz() const { return bayan_f0_ * bayan_pm_; }

 private:
  struct Mode2 {          // one complex (rotating) membrane mode
    float cr, ci, re, im;
    float drive;
  };
  struct Drum {
    Mode2* modes;
    int count;
    int contact, contact_len;
    float strike_amp, impulse_norm;
    float noise_env, noise_decay;
    float extra_damp[6];
    Svf noise_bp;
  };
  struct String {
    float* line;
    float v, v_target, v_base;
    float g, a, lp, hp_x, hp_y;
    float ap[3];
    float jaw_s, jaw_avg;
    int ex_pos, ex_len;
    float ex_amp;
    float level;     // smoothed y^2
    float target;    // rms to hold while frozen
    float boost;     // extra loop gain that holds it
  };

  void StrikeDrum(Drum* d, const float* drive, float amp, float contact_ms,
                  const float* damp, float noise, float noise_hz);
  void UpdateDrum(Drum* d, const float* ratios, const float* t60s,
                  const uint8_t* groups, float f0);
  float TickDrum(Drum* d, float input);
  void TuneStrings(float sa, bool frozen);
  void Pluck(int i, float amp);

  Drum dayan_, bayan_;
  String string_[kStrings];
  Svf body_[4];
  float string_hz_[kStrings];
  float dayan_f0_, bayan_f0_, bayan_pm_;
  float ghe_env_;
  int ghe_dir_;
  float jaw_depth_;
  float pluck_timer_;
  int pluck_step_;
  float tanpura_level_;
  float last_sa_;
  bool last_frozen_;
  bool frozen_;
  size_t write_;
  Random rng_;
  DcBlocker dc_[2];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_TANTRI_H_
