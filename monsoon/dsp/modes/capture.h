// Mode 9: CAPTURE - a wavetable voice built from what you just played in.
//
// The input is recorded into a 0.5 s mono buffer. FREEZE stops recording
// and cuts the buffer into 16 single-cycle frames: each frame's pitch
// period is found (normalised autocorrelation), one period is resampled to
// 1024 points with a loop crossfade, and its harmonics are rebuilt by FFT
// into 8 band-limited mip levels (128 -> 1 harmonics, 8x oversampled). Playback picks the
// mip level from the note so it never aliases.
//
// Per-mode knobs:
//   BAL  noon = v1.0. Left: phase-distortion bend (the first half of the
//        cycle is read up to 6x faster, CZ style). Right: sync formant (the
//        table is read up to 6x per cycle under a smooth window, a
//        resonant peak that sweeps up). The mip level follows, so it stays
//        clean.
//   FBK  self-FM: each voice's own output bends its read phase (DX-style
//        operator feedback): warmer, then saw-like, then rasping noise.
//   RVB  the engine's FDN reverb (a dry voice with no space of its own).

#ifndef MONSOON_DSP_MODES_CAPTURE_H_
#define MONSOON_DSP_MODES_CAPTURE_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/sample_buffer.h"

namespace monsoon {

class CaptureMode : public Mode {
 public:
  static const int kFrames = 16;
  static const int kLevels = 8;
  static const size_t kCycle = 1024;       // samples of level 0
  static const size_t kFrameSize = 2040;   // all levels of one frame
  static const int kMaxVoices = 7;

  const char* name() const { return "capture"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();

  size_t GetPersistentData(PersistentChunk* chunks);
  bool LoadPersistentData(const uint8_t* data, size_t size);
  void SetMorphSource(const uint8_t* data, size_t size);
  bool supports_morph() const { return true; }
  float reverb_amount(const Controls& c) const { return c.rvb; }
  bool frozen() const { return frozen_; }

  // For tests.
  bool building() const { return build_frame_ >= 0; }
  float detected_period(int frame) const { return period_[frame]; }
  bool morph_active() const { return morph_table_ != NULL; }

  struct SlotHeader {
    uint32_t magic;
    uint16_t frames;
    uint16_t frame_size;
  };

 private:
  void BuildLevels(int frame);
  void BuildFrameFromCapture(int frame);
  void BuildDefaultFrame(int frame);
  float DetectPeriod(int32_t start);
  inline const int16_t* FramePtr(int frame) const;

  SampleBuffer capture_;
  int16_t* table_;
  const int16_t* morph_table_;
  float* work_;
  float* spectrum_;
  float* temp_;
  float period_[kFrames];

  float phase_[kMaxVoices];
  float fm_[kMaxVoices];     // FBK: each voice's recent output
  float fm_depth_;           // FBK, smoothed
  float bal_;                // BAL, bipolar and smoothed
  float envelope_;
  int env_stage_;  // 0 idle/release, 1 attack, 2 decay/sustain
  bool gated_;
  bool previous_gate_;
  bool previous_freeze_;
  bool frozen_;
  int build_frame_;          // -1 when idle
  uint32_t build_head_;
  uint32_t build_filled_;
  SlotHeader header_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CAPTURE_H_
