// Mode 19 - HARMONIZER: scale-quantised harmonizer.
//
// The input's pitch is tracked (McLeod NSDF on a 12 kHz copy, every
// 10.7 ms, in Prepare()). The tracked note is snapped to the scale, and up to
// three voices are placed a number of scale steps above/below it. Each
// voice is a pitch-synchronous two-tap shifter whose ratio is
// target / tracked pitch, so the voices land exactly on the scale degrees
// even when the input is a little sharp or flat. The wet output is the
// input plus the voices.
//
// Chord sets (POSITION, 8 zones; steps along the scale, "oct" = a whole
// scale): 1 3rd up | 2 5th up | 3 3rd + 5th | 4 octave up + down |
// 5 3rd below + 3rd above | 6 4th + 7th | 7 3rd + 5th + octave |
// 8 5th + 9th + octave down.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | chord set (above)                                           |
// | PITCH    | key root, V/oct; C at noon, each semitone moves the key     |
// | DENSITY  | how many voices of the set (1 -> 3) and their level         |
// | SIZE     | humanise: voice detune (up to +/-15 cents) and delay (25 ms)|
// | TEXTURE  | stereo spread of the voices                                 |
// | FREEZE   | latch: the voices keep their current notes                  |
// | TRIG     | next chord set                                              |
// | options  | scale: 0 major, 1 natural minor, 2 dorian, 3 major          |
// |          | pentatonic (LED 1-4 green)                                  |
//
// Per-mode knobs:
//   BAL  register of the voices: noon = v1.0, left third = every voice an
//        octave lower, right third = an octave higher (bass harmony /
//        descant), the chord shape unchanged.
//   FBK  canon: the voices (not your input) echo every 340 ms, darker
//        each time; FBK raises the send and the repeats (up to 0.75
//        feedback).
//   RVB  glide: the voices slide into each new note like singers finding
//        the chord, 10 ms (v1.0) -> ~0.8 s portamento.

#ifndef MONSOON_DSP_MODES_HARMONIZER_H_
#define MONSOON_DSP_MODES_HARMONIZER_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class HarmonizerMode : public Mode {
 public:
  static const int kVoices = 3;
  static const size_t kRingSize = 4096;      // 48 kHz mono, power of two
  static const size_t kDecSize = 1024;       // 12 kHz mono
  static const size_t kWindow = 384;         // analysis window (12 kHz)
  static const int kMinLag = 12;             // 1 kHz
  static const int kMaxLag = 200;            // 60 Hz
  static const size_t kEchoSize = 16384;     // 341 ms, int16

  const char* name() const { return "harmonizer"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();
  uint8_t num_options() const { return 4; }
  bool frozen() const { return latched_; }

  // Tracker state (for tests).
  float tracked_hz() const { return tracked_hz_; }
  bool voiced() const { return voiced_; }
  // Target note (MIDI) of voice v for a tracked note, root and scale.
  static float TargetNote(float input_note, int root, int scale, int set,
                          int voice, int* count);

 private:
  void Analyse();

  float* ring_;       // input (mid), 48 kHz
  float* dec_;        // decimated copy, 12 kHz
  float* nsdf_;       // lag workspace
  size_t write_;
  size_t dec_write_;
  float dec_acc_;
  int dec_count_;
  volatile uint32_t dec_hops_;   // written by Process
  uint32_t dec_hops_done_;

  volatile float tracked_hz_;
  volatile bool voiced_;
  float period_;      // smoothed period (48 kHz samples)

  float phase_[kVoices];
  float log_ratio_[kVoices];     // smoothed, semitones
  float latched_note_[kVoices];
  bool latched_;
  bool was_trigger_;
  int set_offset_;
  float gain_[kVoices];

  // Knobs.
  float glide_note_[kVoices];    // RVB: gliding target note (MIDI)
  int16_t* echo_;                // FBK: canon line
  size_t echo_pos_;
  float echo_lp_;
  float echo_amt_;               // smoothed FBK
  int32_t echo_tail_;            // samples left to drain after FBK -> 0
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_HARMONIZER_H_
