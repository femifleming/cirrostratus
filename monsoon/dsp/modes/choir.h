// Mode 24 - CHOIR: four voices on a chord, through vowel formants.
//
// Controls
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | vowel a -> e -> i -> o -> u (interpolated formants)          |
// | DENSITY  | chord, 6 zones: unison, fifths, major, minor, sus4, dom7     |
// | SIZE     | voice spread: detune (up to +/-20 cents), stereo width,      |
// |          | vibrato depth                                                |
// | TEXTURE  | excitation: 0 = the input, pitched by a comb at each note;   |
// |          | 1 = internal glottal source (with breath); between = mix     |
// | PITCH    | chord root, V/oct (C3 = 130.81 Hz at noon)                   |
// | FREEZE   | sustain the chord on the internal source at full level       |
// |          | (no input needed)                                            |
// | TRIG     | next inversion (root, 1st, 2nd, 3rd, back to root)           |
// | input    | excites the combs; its envelope also drives the glottal      |
// |          | source when not frozen                                       |
// | options  | none                                                         |
//
// Each voice = glottal pulse train (a raised half-sine of 60 % open
// quotient, slight vibrato) and/or the input through a feedback comb tuned
// to the voice's note. Voices are panned, summed per side, and shaped by
// five parallel formant band-passes (alto table, as in Csound's formant
// data).

#ifndef MONSOON_DSP_MODES_CHOIR_H_
#define MONSOON_DSP_MODES_CHOIR_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class ChoirMode : public Mode {
 public:
  static const int kVoices = 4;
  static const int kFormants = 5;
  static const size_t kCombSize = 2048;
  static const size_t kCombMask = kCombSize - 1;

  const char* name() const { return "choir"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  bool frozen() const { return frozen_; }

  // Test hooks.
  float voice_hz(int v) const { return voice_hz_[v]; }
  int inversion() const { return inversion_; }

 private:
  struct Voice {
    float* comb;
    float phase;
    float vib_phase;
    float vib_rate;
    float damp;       // comb loop low-pass state
  };

  void UpdateFormants(float position);

  Voice voice_[kVoices];
  float voice_hz_[kVoices];
  float pan_[kVoices];
  Svf formant_[2][kFormants];
  float formant_gain_[kFormants];
  float last_position_;
  size_t write_;
  int inversion_;
  bool frozen_;
  Envelope env_;
  float glottal_level_;
  Random rng_;
  DcBlocker dc_[2];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CHOIR_H_
