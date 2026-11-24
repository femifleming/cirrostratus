// Mode 8: SPREEK - voice.
//
// Two filters shape a source:
//  - LPC (order 14) of the input, analysed every 256 samples in Prepare(),
//    run as a lattice so coefficients can glide safely (talkbox).
//  - 5 parallel formant resonators interpolating the vowels a-e-i-o-u.
// The source is the input itself (its LPC residual for the talkbox path)
// or an internal band-limited glottal sawtooth at PITCH.

#ifndef MONSOON_DSP_MODES_SPREEK_H_
#define MONSOON_DSP_MODES_SPREEK_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class SpreekMode : public Mode {
 public:
  static const int kOrder = 14;
  static const int kFormants = 5;
  static const size_t kWindow = 512;
  static const size_t kHop = 256;

  const char* name() const { return "spreek"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();
  // 0 = adult formants, 1 = child formants.
  uint8_t num_options() const { return 2; }
  bool frozen() const { return frozen_; }

 private:
  inline float Analyse(float x);
  inline float Synthesise(float e);

  // Analysis hand-off between the interrupt and Prepare().
  float* ring_;
  float* frame_;
  size_t ring_pos_;
  size_t hop_count_;
  volatile bool frame_pending_;
  volatile bool coeffs_ready_;
  float k_new_[kOrder];
  float e_new_;

  float k_[kOrder];
  float k_target_[kOrder];
  float e_;
  float e_target_;
  float ba_[kOrder];      // analysis lattice state
  float bs_[kOrder];      // synthesis lattice state
  float pre_x1_;
  float de_y1_;

  Svf formant_[kFormants];
  float f_freq_[kFormants], f_amp_[kFormants], f_q_[kFormants];

  float phase_;
  float lpc_mix_;
  Envelope env_;
  float out_env_;
  float agc_;
  float held_env_;
  float held_position_;
  float burst_;
  Svf burst_filter_;
  Random rng_;
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_SPREEK_H_
