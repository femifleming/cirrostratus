// Mode 10 - GENDY: dynamic stochastic synthesis after Iannis Xenakis
// (GENDYN). A waveform made of K breakpoints is played as straight line
// segments; every time the playhead passes a breakpoint, that breakpoint's
// amplitude and duration take a random step drawn from a probability
// distribution, reflected off mirror barriers. Two independent voices
// (left / right). Rendered at 96 kHz and decimated with a half-band FIR,
// so the corners of the segments do not alias.

#ifndef MONSOON_DSP_MODES_GENDY_H_
#define MONSOON_DSP_MODES_GENDY_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class GendyMode : public Mode {
 public:
  static const int kMaxBreakpoints = 40;
  static const int kMinBreakpoints = 3;
  static const size_t kHistorySize = 2048;   // input history (reseeding)
  static const size_t kFirRing = 64;

  const char* name() const { return "gendy"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

  size_t GetPersistentData(PersistentChunk* chunks);
  bool LoadPersistentData(const uint8_t* data, size_t size);

  // Test hooks.
  int num_breakpoints() const { return state_->num_breakpoints; }
  float breakpoint_amplitude(int voice, int i) const {
    return state_->amplitude[voice][i];
  }

  // Everything that gets saved lives in one struct in the arena.
  struct State {
    uint32_t magic;
    uint32_t num_breakpoints;
    float amplitude[2][kMaxBreakpoints];
    float duration[2][kMaxBreakpoints];   // relative to the mean (1.0)
  };

 private:
  struct Voice {
    int index;           // current breakpoint
    float position;      // samples (96 kHz) into the current segment
    float length;        // length of the current segment
    float from, to;      // amplitudes at the segment ends
    size_t fir_write;
  };

  float Distribution(float u) const;
  void Step(int voice, int index);
  float Render96k(int voice);
  void Reseed();

  State* state_;
  float* history_;
  float* fir_ring_[2];
  size_t history_write_;
  Voice voice_[2];

  // Block-rate parameters.
  float mean_length_;          // samples at 96 kHz per breakpoint
  float amp_step_, dur_step_;
  float barrier_;              // duration mirror half-width
  float dist_zone_;            // 0..3, selects/morphs the distribution
  float period_1x_;            // period in 48 kHz samples (for reseeding)
  float halfband_[12];         // unique odd taps of the half-band FIR
  bool frozen_;
  Random rng_;
  DcBlocker dc_[2];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_GENDY_H_
