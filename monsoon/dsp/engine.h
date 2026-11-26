// The Monsoon HiFi engine: mode host + output section.

#ifndef MONSOON_DSP_ENGINE_H_
#define MONSOON_DSP_ENGINE_H_

#include "monsoon/dsp/core.h"
#include "monsoon/dsp/fx.h"
#include "monsoon/dsp/mode.h"

namespace monsoon {

// Panel numbers are fixed: slots, settings (per-mode arrays) and the menu
// all index by them. Mode 1 (GRAINS) was removed in 1.5: its number stays
// reserved, so every other mode keeps its number and old settings,
// calibration and slots still load.
enum ModeId {
  MODE_REMOVED_1,    // 1  (was GRAINS, removed in 1.5): never selected
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
  MODE_CHLOE,        // 14 Chloaelis strip: filter > tube > delay
  MODE_FALTUNG,     // 15  IR library convolution
  MODE_QUELL,       // 16  resonance suppressor
  MODE_LEITNER,     // 17  spatial path panner
  MODE_TANTRI,      // 18  tabla + tanpura
  MODE_HARMONIZER,  // 19  scale harmonizer
  MODE_PUTZ,        // 20  spectral cleaner
  MODE_ENSEMBLE,    // 21  string ensemble chorus
  MODE_BARBERPOLE,  // 22  frequency-shift spirals
  MODE_SPECDELAY,   // 23  spectral delay
  MODE_CHOIR,       // 24  formant choir
  MODE_FOLDER,      // 25  wavefolder + LPG
  MODE_HALL,        // 26  big reverb
  MODE_BLOOM,       // 27  swells + reverse bloom
  MODE_GAMELAN,     // 28  pelog/slendro metallophones
  MODE_MIRROR,      // 29  spectral warping
  MODE_POLISH,      // 30  mastering chain
  MODE_LAST,
  MODE_FIRST = MODE_SPECTRAL  // the lowest mode that exists
};

inline bool ModeExists(int m) { return m >= MODE_FIRST && m < MODE_LAST; }

// What the envelope page can push: WET and the three per-mode knobs.
enum EnvTarget {
  ENV_TARGET_BLEND,
  ENV_TARGET_BAL,
  ENV_TARGET_FBK,
  ENV_TARGET_RVB,
  ENV_TARGET_LAST
};

// How strongly the input envelope pushes each knob (-1..+1).
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
  // The audio interrupt ran past its share of the CPU: the next block
  // skips the mode (its wet is silent for that block, the fades, the dry
  // path and the output section go on), which leaves the main loop the
  // time to read the panel and change modes. A mode that is too heavy is
  // heard to drop out, never locks the module.
  void set_shed(bool shed) { shed_ = shed; }
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

  // Mono bass on the main out (LR4 crossover at 60 Hz). On by default.
  // `immediate` skips the 20 ms crossfade (start-up, tests).
  void set_mono_bass(bool on, bool immediate = false) {
    mono_bass_.set_on(on, immediate);
  }
  bool mono_bass() const { return mono_bass_.on(); }

  // Master buffer quality (mode.h): every mode that records into a
  // SampleBuffer uses it. Changing it re-inits such a mode (its buffer is
  // cleared). A loaded slot plays at the quality it was saved with.
  void set_buffer_quality(uint8_t q);
  uint8_t buffer_quality() const { return buffer_quality_; }

  // Master FFT size (mode.h, 0 = each mode's own) for the spectral modes;
  // the same scheme: changing it re-inits such a mode, a loaded slot plays
  // at the size it was saved with, and a size that doesn't fit in memory
  // falls back to the mode's own.
  void set_fft_setting(uint8_t s);
  uint8_t fft_setting() const { return fft_setting_; }
  // The running mode's own FFT size (0: not a spectral mode).
  size_t native_fft_size() const { return current_->native_fft_size(); }
  // The FFT size the running mode uses (0: not a spectral mode).
  size_t fft_size() const {
    return current_->uses_fft_size() ? current_->FftSize() : 0;
  }

  // Mute the wet path (used while flash is being written).
  void set_silence(bool s) { silence_ = s; }

  // Diagnostics.
  size_t arena_used() const { return arena_.used(); }
  size_t arena_capacity() const { return arena_.capacity(); }
  bool init_failed() const { return init_failed_; }
  float tamer_depth() const { return tamer_.depth(); }
#ifdef TEST
  // What the mode saw in the last block (after the envelope routing).
  const Controls& last_controls() const { return last_controls_; }
#endif
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
  bool shed_;
  bool shed_last_;        // the previous block was shed: fade the mode back in
  FloatFrame last_wet_;   // the mode's last output, for a shed block's ramp
  volatile uint8_t buffer_quality_;
  volatile uint8_t fft_setting_;

  const uint8_t* pending_load_data_;
  size_t pending_load_size_;

#ifdef TEST
  Controls last_controls_;
#endif
  EnvRouting env_routing_;
  Envelope envelope_;

  // Services a mode may map a knob to (see Mode::feedback_amount).
  Fdn reverb_;
  ResonanceTamer tamer_;
  bool feedback_active_;
  MonoBass mono_bass_;
  Limiter limiter_;
  OnePole fb_hp_[2];
  DcBlocker out_dc_[2];
  Random dither_;

  BlockRamp blend_, feedback_;
  FloatFrame feedback_signal_[kBlockSize];
  FloatFrame dry_[kBlockSize];
  FloatFrame mode_in_[kBlockSize];
  FloatFrame wet_[kBlockSize];
};

}  // namespace monsoon

#endif  // MONSOON_DSP_ENGINE_H_
