// Mode 1: GRAINS - trajectory grains.
//
// A granular cloud whose grains are placed along a moving path through the
// recording buffer (circle, line, figure-8, spiral, random walk). The path
// also carries a pan coordinate, so circular paths swirl across the stereo
// field.
//
// Per-mode knobs:
//   BAL  noon = v1.0. Left: spray (grains scatter around the path, up to
//        half the buffer). Right: share of grains played backwards (all of
//        them at full).
//   FBK  the engine's feedback loop (with the resonance tamer).
//   RVB  the engine's FDN reverb.

#ifndef MONSOON_DSP_MODES_GRAINS_H_
#define MONSOON_DSP_MODES_GRAINS_H_

#include "monsoon/dsp/mode.h"
#include "monsoon/dsp/sample_buffer.h"

namespace monsoon {

class GrainsMode : public Mode {
 public:
  enum Shape {
    SHAPE_CIRCLE,
    SHAPE_LINE,
    SHAPE_FIGURE_8,
    SHAPE_SPIRAL,
    SHAPE_RANDOM_WALK,
    SHAPE_LAST
  };

  static const int kMaxGrains = 32;

  const char* name() const { return "grains"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);

  // Options 0-3 stereo (chromatic, minor pentatonic, octaves+fifths, free),
  // 4-7 the same with a mono buffer of twice the length.
  uint8_t num_options() const { return 8; }
  bool option_requires_init() const { return true; }
  bool uses_buffer_quality() const { return true; }

  size_t GetPersistentData(PersistentChunk* chunks) {
    return buffer_.GetChunks(chunks);
  }
  bool LoadPersistentData(const uint8_t* data, size_t size) {
    return buffer_.Load(data, size) != 0;
  }
  bool frozen() const { return frozen_; }

  // FBK and RVB use the engine's feedback loop and FDN reverb (the Clouds
  // pairing for a granular cloud).
  float feedback_amount(const Controls& c) const { return c.fbk; }
  float reverb_amount(const Controls& c) const { return c.rvb; }

  // For tests.
  Shape shape() const { return shape_; }
  float buffer_seconds() const { return buffer_.seconds(); }
  int active_grains() const;
  static float Quantize(float semitones, int scale);

 private:
  struct Grain {
    float position;   // absolute buffer position (frames)
    float increment;  // playback rate
    float phase;      // 0..1 through the window
    float phase_inc;
    float gain_l;
    float gain_r;
    float texture;
    bool active;
  };

  void Spawn(const Controls& c, float rate, float length);
  void TrajectoryPoint(float* x, float* y);
  inline float Window(float u, float texture) const;
  template<bool kLofi> void Render(FloatFrame* out, size_t n);

  SampleBuffer buffer_;
  Grain* grains_;
  Random rng_;
  Shape shape_;
  float theta_;           // trajectory phase, cycles
  float walk_x_, walk_y_;
  float walk_vx_, walk_vy_;
  float spawn_acc_;
  float next_interval_;
  bool frozen_;
  bool previous_trigger_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_GRAINS_H_
