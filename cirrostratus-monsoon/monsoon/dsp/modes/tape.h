// Mode 12: TAPE - multi-head tape echo.
//
// The tape is a block-floating-point loop (SampleBuffer). The tape moves
// at a variable speed: the input is anti-alias filtered and resampled onto
// the tape, so slower tape stores fewer samples per second - longer and
// darker echoes, exactly like the real thing. Three playback heads sit at
// 1, 2 and 3 x the head spacing behind the record head.
//
// Per-mode knobs (they act on the repeats, not on FREEZE's held loop):
//   BAL  noon = v1.0. Left: the repeats get darker (loop low-pass 7 kHz ->
//        500 Hz). Right: the repeats get thinner (loop high-pass up to
//        2 kHz), the dub telephone fade.
//   FBK  ping-pong: the repeats cross over to the other channel (fully
//        right = every repeat swaps sides). Stereo tape only.
//   RVB  diffusion in the loop: a chain of allpasses smears every pass a
//        little more, so the echoes melt into a spring-like wash.

#ifndef MONSOON_DSP_MODES_TAPE_H_
#define MONSOON_DSP_MODES_TAPE_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/sample_buffer.h"

namespace monsoon {

class TapeMode : public Mode {
 public:
  static const int kHeads = 3;

  const char* name() const { return "tape"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  // 0 = stereo tape, 1 = mono tape (twice as long).
  uint8_t num_options() const { return 2; }
  bool option_requires_init() const { return true; }
  bool uses_buffer_quality() const { return true; }
  // Longest echo (head 3) at normal tape speed, seconds (tests).
  float head3_seconds() const {
    return 3.0f * spacing_ / (kSampleRate * tape_rate_);
  }
  size_t GetPersistentData(PersistentChunk* chunks) {
    return tape_.GetChunks(chunks);
  }
  bool LoadPersistentData(const uint8_t* data, size_t size) {
    if (!tape_.Load(data, size)) return false;
    staged_ = 0;
    acc_ = 0.0f;
    return true;
  }
  bool frozen() const { return frozen_; }

  // Delay in seconds of head k (0..2) at the current speed (for tests).
  float head_delay_seconds(int k) const {
    return spacing_ * (k + 1) / (speed_ * kSampleRate + 1e-9f);
  }
  float speed() const { return speed_; }

 private:
  void WriteTapeSample(float l, float r);
  template<bool kLofi> inline float ReadHead(int channel, float distance) const;
  template<bool kLofi> void ProcessT(const Controls& c, const FloatFrame* in,
                                     FloatFrame* out, size_t n);

  SampleBuffer tape_;
  float tape_rate_;  // 1, 1/2, 1/3: tape speed scale from the buffer quality
  FloatFrame stage_[kBlockSize];
  size_t staged_;
  int channels_;
  float acc_;
  float spacing_;           // tape samples between heads
  float hist_[2][4];        // input history for the resampler
  Svf aa_[2][3];            // 6-pole anti-alias filter before the tape
  Svf bump_[2];             // head bump
  OnePole loss_[2];         // repeat loss in the loop
  OnePole thin_[2];         // BAL right: high-pass in the loop
  float* diffuser_[2];      // RVB: four allpasses per channel
  uint16_t ap_pos_[4];
  float diffuse_mix_;
  float fb_[2];
  float head_gain_[kHeads];
  float fb_gain_[kHeads];
  float speed_;             // actual tape speed (after stop/start)
  float speed_target_;
  float stop_mult_;
  bool stopped_;
  float input_gain_;
  float wow_phase_, flutter_phase_, flutter_noise_;
  Random rng_;
  bool frozen_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_TAPE_H_
