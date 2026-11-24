// The Monsoon HiFi engine: mode host + output section.

#ifndef MONSOON_DSP_ENGINE_H_
#define MONSOON_DSP_ENGINE_H_

#include "monsoon/dsp/core.h"
#include "monsoon/dsp/fx.h"
#include "monsoon/dsp/mode.h"

namespace monsoon {

enum ModeId {
  MODE_GRAINS,       // 1  trajectory grains
  MODE_SPECTRAL,     // 2  spectral freeze / resynthesis
  MODE_CONVOLVE,     // 3  convolution, freeze captures the IR
  MODE_RESONATOR,    // 4  sympathetic strings + jawari
  MODE_DIFFERENCE,   // 5  difference tones
  MODE_CROSS,        // 6  L/R spectral morph
  MODE_STRETCH,      // 7  extreme time stretch
  MODE_SPREEK,       // 8  voice / formants / talkbox
  MODE_CAPTURE,      // 9  captured wavetable voice
  MODE_GENDY,        // 10 dynamic stochastic synthesis
  MODE_SHIMMER,      // 11 shimmer cascade
  MODE_TAPE,         // 12 multi-head tape echo
  MODE_PLATES,       // 13 sympathetic plates (modal)
  MODE_LAST
};

enum EnvTarget {
  ENV_TARGET_BLEND,
  ENV_TARGET_SPREAD,
  ENV_TARGET_FEEDBACK,
  ENV_TARGET_REVERB,
  ENV_TARGET_LAST
};

// How strongly the input envelope pushes each output knob (-1..+1).
struct EnvRouting {
  float amount[ENV_TARGET_LAST];
};

class Engine {
 public:
  // main/ccm are the two big RAM blocks. The reverb lives in `ccm`.
  bool Init(void* main_mem, size_t main_size, void* ccm_mem, size_t ccm_size);

  // Audio interrupt (firmware): int16 codec frames.
  void Process(const Controls& raw, const ShortFrame* in, ShortFrame* out,
               size_t n);
  // Same, floating point (harness).
  void ProcessFloat(const Controls& raw, const FloatFrame* in,
                    FloatFrame* out, size_t n);

  // Main loop.
  void Prepare();

  // Mode switching happens with a short fade, the heavy Init in Prepare().
  void RequestMode(ModeId mode, uint8_t option);
  ModeId mode() const { return requested_mode_; }
  uint8_t option() const { return requested_option_; }
  bool switching() const { return state_ != STATE_RUNNING; }
  Mode* mode_instance(ModeId id);
  static const char* mode_name(ModeId id);
  uint8_t num_options(ModeId id);

  void set_env_routing(const EnvRouting& r) { env_routing_ = r; }
  const EnvRouting& env_routing() const { return env_routing_; }

  // Save slots.
  size_t GetPersistentData(PersistentChunk* chunks);
  void RequestLoad(const uint8_t* data, size_t size);
  void SetMorphSource(const uint8_t* data, size_t size);
  bool supports_morph();

  // Mute the wet path (used while flash is being written).
  void set_silence(bool s) { silence_ = s; }

  // Diagnostics.
  size_t arena_used() const { return arena_.used(); }
  size_t arena_capacity() const { return arena_.capacity(); }
  bool init_failed() const { return init_failed_; }
  float tamer_depth() const { return tamer_.depth(); }
  float envelope() const { return envelope_.value(); }

 private:
  enum State {
    STATE_RUNNING,
    STATE_FADING_OUT,
    STATE_SWITCH_PENDING,
    STATE_LOAD_PENDING,
    STATE_FADING_IN
  };

  void ApplyEnvelope(const Controls& raw, Controls* c);

  Arena arena_;
  Arena reverb_arena_;
  Mode* current_;
  volatile ModeId requested_mode_;
  volatile uint8_t requested_option_;
  ModeId active_mode_;
  uint8_t active_option_;
  volatile State state_;
  float fade_;
  bool silence_;
  bool init_failed_;

  const uint8_t* pending_load_data_;
  size_t pending_load_size_;

  EnvRouting env_routing_;
  Envelope envelope_;

  Fdn reverb_;
  ResonanceTamer tamer_;
  Spread spread_;
  Limiter limiter_;
  OnePole fb_hp_[2];
  DcBlocker out_dc_[2];
  Random dither_;

  BlockRamp blend_, spread_amt_, feedback_, reverb_amt_;
  FloatFrame feedback_signal_[kBlockSize];
  FloatFrame dry_[kBlockSize];
  FloatFrame mode_in_[kBlockSize];
  FloatFrame wet_[kBlockSize];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_ENGINE_H_
