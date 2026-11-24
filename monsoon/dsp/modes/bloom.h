// Mode 27 - BLOOM: swells and reverse blooms.
//
// The input is recorded into a stereo block-float buffer and played back
// late by twice the reverse window W. Two reverse heads (half a window
// apart) read the buffer backwards from "now", each with a rising
// envelope, so every chunk of sound is heard reversed and swelling *before*
// the delayed dry reaches it: a reversed head finishes exactly on the
// sample the dry is playing at that moment. The delayed dry also passes a
// transient softener whose attack time is set by POSITION, so attacks
// swell in instead of striking. TEXTURE diffuses the reverse heads (a
// chain of allpasses) into a reverse-reverb-like haze.
//
// Latency (the delayed dry) = 2 W: 0.1 s ... 0.56 s.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | swell time of the delayed dry: 0 = attacks untouched,       |
// |          | 1 = ~2 s swell on every note                                |
// | SIZE     | reverse window W, 50 ms -> 280 ms (= half the latency)      |
// | DENSITY  | reverse-bloom level (0 = only the swelled dry)              |
// | TEXTURE  | diffusion of the reverse bloom (dry reversed -> haze)       |
// | PITCH    | transposition of the reverse bloom (+/-12 st, noon = none)  |
// | FREEZE   | stop recording: the dry fades out and the last window keeps |
// |          | blooming in reverse, over and over                          |
// | TRIG     | restart the reverse heads now                               |
// | options  | none                                                        |
//
// Per-mode knobs:
//   BAL  noon = v1.0 (every bloom reversed, swelling in). Left: a growing
//        share of blooms fade *out* instead (reverse echoes that strike,
//        then trail away). Right: a growing share play *forwards*,
//        swelling in (a swelled echo, one window late). Chosen per chunk.
//   FBK  regeneration: the engine's feedback loop. The output (swelled
//        dry and blooms) re-enters the buffer, so every bloom blooms
//        again, reversed twice = forwards again ...: cascades, one round
//        per 2 W.
//   RVB  haze tail: TEXTURE's allpass chain is closed into a damped loop
//        (gain up to 0.85, ~1.3 s), so the blooms - only they - ring on
//        after the note: reverse reverb -> note -> reverb. Brings at
//        least TEXTURE ~0.8's diffusion with it.
//

#ifndef MONSOON_DSP_MODES_BLOOM_H_
#define MONSOON_DSP_MODES_BLOOM_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/sample_buffer.h"

namespace monsoon {

class BloomMode : public Mode {
 public:
  static const int kStages = 4;

  const char* name() const { return "bloom"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

  // FBK is the engine's feedback loop.
  float feedback_amount(const Controls& c) const { return c.fbk; }

  // Delay of the dry path in samples for a SIZE value (for tests).
  static float WindowSamples(float size);

 private:
  __attribute__((noinline, cold)) void PickChunk(int h, float bal);

  struct Diffuser {
    float* line;
    size_t size;
    size_t pos;
  };

  SampleBuffer buffer_;
  Diffuser ap_[2][kStages];
  float window_;        // smoothed W (samples)
  float phase_[2];      // head phases 0..1
  float start_[2];      // buffer position each head started from
  float env_fast_, env_slow_;
  float dry_gain_;
  float haze_[2];       // RVB: last output of each diffusion chain (damped)
  Random rng_;          // BAL: per-chunk direction / envelope choice
  bool next_forward_[2];
  bool next_fall_[2];
  float off_[2], dir_[2];    // read position: start - (off + dir ph) W
  float rise_[2], fade_[2];  // envelope knee and fade length
  bool frozen_;
  bool was_trigger_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_BLOOM_H_
