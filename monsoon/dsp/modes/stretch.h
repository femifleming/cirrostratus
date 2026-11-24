// Mode 7: STRETCH - extreme time stretch (phase vocoder <-> Paulstretch).
//
// A recording buffer is read by a slowly moving analysis window. Each
// synthesis frame keeps per-bin running phases driven by instantaneous
// frequencies (coherent, phase-vocoder sound) and TEXTURE blends in random
// phases (Paulstretch smear). Uses its own lean STFT (it never analyses
// the live input, so it needs no input ring), which frees memory for the
// buffer.
//
// Per-mode knobs:
//   BAL  noon = v1.0. Left: the read slows, stands still at 9 o'clock and
//        runs backwards at full left (at the SIZE stretch). Right: scatter,
//        the read window jumps to random spots up to half the buffer away,
//        up to ~10 jumps per second.
//   FBK  re-record: the output is mixed back into the recording (x 0.85
//        at full, input trimmed to keep the level, soft-clipped), so
//        stretches get stretched again and smear into drones. Nothing is
//        recorded while FREEZE is on.
//   RVB  octave halo: every bin also sounds an octave up (phase-doubled, so
//        it stays coherent), up to -3 dB: a shimmer over the stretch.

#ifndef MONSOON_DSP_MODES_STRETCH_H_
#define MONSOON_DSP_MODES_STRETCH_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/sample_buffer.h"

namespace monsoon {

class StretchMode : public Mode {
 public:
  static const size_t kFftSize = 2048;
  static const size_t kHop = 683;  // ~N/3: 3x overlap, Hann^2 sums to 1.5
  static const size_t kBins = kFftSize / 2;

  const char* name() const { return "stretch"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();

  // 0 stereo buffer, 1 mono buffer (twice as long).
  uint8_t num_options() const { return 2; }
  bool option_requires_init() const { return true; }

  size_t GetPersistentData(PersistentChunk* chunks) {
    return buffer_.GetChunks(chunks);
  }
  bool LoadPersistentData(const uint8_t* data, size_t size) {
    return buffer_.Load(data, size) != 0;
  }
  bool frozen() const { return controls_.freeze; }

  // For tests.
  float buffer_seconds() const { return buffer_.seconds(); }
  const float* inst(int ch) const { return inst_[ch]; }
  // Bins above this (20.0 kHz) are not processed.
  static const size_t kTopBin = 854;
  size_t latency() const { return kFftSize + kHop; }
  // Static stretch-factor mapping: frames advance by kHop / factor.
  static float AdvancePerHop(float size);

 private:
  // Windowed analysis of the buffer at `start` into spectrum_.
  void Analyse(int ch, int32_t start);
  void StoreReference(int ch);
  void Synthesize(int ch, float dist, bool update_ref, bool reset_ref);
  void OverlapAdd(int ch, size_t out_start);

  SampleBuffer buffer_;
  int channels_;
  size_t ring_size_;
  float* ring_[2];
  float* frame_;     // time-domain frame / output spectrum
  float* spectrum_;  // analysis spectrum / inverse output
  int16_t* ref_[2];       // unit vector of the reference frame, per bin
  uint16_t* synth_phase_[2];
  float* inst_[2];
  float* smear_[2];

  Controls controls_;  // latest controls, read by Prepare()
  Random rng_;
  float window_cos_;  // rotation step for the sine window
  float window_sin_;
  size_t pos_;
  size_t hop_counter_;
  volatile uint32_t hops_ready_;
  uint32_t hops_done_;
  volatile uint32_t written_;
  volatile bool jump_pending_;
  bool previous_trigger_;

  float drift_;
  uint32_t ref_written_;
  float ref_delay_;
  bool have_ref_;
  uint32_t hops_since_ref_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_STRETCH_H_
