// Interface every Monsoon HiFi mode implements.

#ifndef MONSOON_DSP_MODE_H_
#define MONSOON_DSP_MODE_H_

#include "monsoon/dsp/core.h"

namespace monsoon {

// A chunk of memory a mode wants written to a flash slot.
struct PersistentChunk {
  const void* data;
  size_t size;
};

const size_t kMaxPersistentChunks = 6;

// Buffer quality (the master setting on the buffer page). The modes that
// record into a SampleBuffer can trade fidelity for length:
//   0 48 kHz 16-bit (v1.0/v1.1)  1 24 kHz 16-bit  2 16 kHz 16-bit
//   3 48 kHz  8-bit              4 24 kHz  8-bit  5 16 kHz  8-bit
const uint8_t kNumBufferQualities = 6;
inline int QualityDecimation(uint8_t q) { return 1 + (q % 3); }
inline bool QualityEightBit(uint8_t q) { return q >= 3 && q < 6; }
// How many times longer the buffer gets.
inline int QualityLengthFactor(uint8_t q) {
  return QualityDecimation(q) * (QualityEightBit(q) ? 2 : 1);
}

class Mode {
 public:
  virtual ~Mode() { }

  // Short name, used by the harness and the manual.
  virtual const char* name() const = 0;

  // Allocate everything from the arena and reset. Called from the main
  // loop while the mode is muted. Returns false if memory ran out.
  virtual bool Init(Arena* arena) = 0;

  // Audio interrupt: process n frames (n == kBlockSize on the module).
  // `in` already contains the feedback signal.
  virtual void Process(const Controls& c, const FloatFrame* in,
                       FloatFrame* out, size_t n) = 0;

  // Main loop: heavy, non-real-time work (FFTs, rebuilding tables).
  virtual void Prepare() { }

  // Number of options selectable in the mode menu (FREEZE cycles them).
  virtual uint8_t num_options() const { return 1; }
  // True if changing the option needs a fresh Init (e.g. mono/stereo
  // buffer layout). Otherwise the option is just read from Controls.
  virtual bool option_requires_init() const { return false; }
  // The option the engine will pass to Init (set before Init is called).
  void set_init_option(uint8_t o) { init_option_ = o; }

  // Save slots. Fill chunks, return count (0 = nothing to save).
  virtual size_t GetPersistentData(PersistentChunk* chunks) {
    (void)chunks;
    return 0;
  }
  // Load from a flash slot (memory-mapped, read-only). The layout is the
  // concatenation of the chunks returned by GetPersistentData.
  virtual bool LoadPersistentData(const uint8_t* data, size_t size) {
    (void)data; (void)size;
    return false;
  }
  // Slot morph: a second slot this mode may read straight from flash
  // (NULL clears it). Only some modes use it.
  virtual void SetMorphSource(const uint8_t* data, size_t size) {
    (void)data; (void)size;
  }
  virtual bool supports_morph() const { return false; }

  // Engine services a mode may map one of its own knobs to (BAL/FBK/RVB
  // are per-mode controls). Return 0 when the service is not used; the
  // engine then skips it and it costs no CPU.
  //  - feedback_amount: the global feedback loop (wet output -> 25 Hz HP ->
  //    resonance tamer -> soft clip -> back into the mode's input), 0..1.
  //  - reverb_amount: the 8-line FDN reverb after the blend, 0..1 (send and
  //    decay together, 0.5 s -> ~16 s).
  virtual float feedback_amount(const Controls& c) const {
    (void)c;
    return 0.0f;
  }
  virtual float reverb_amount(const Controls& c) const {
    (void)c;
    return 0.0f;
  }

  // Whether the mode wants the dry input muted while frozen (purely a hint
  // for the engine's meter; blending still uses the BLEND slider).
  virtual bool frozen() const { return false; }

  // Buffer quality (see above). Set by the engine before Init; a mode
  // that records into a SampleBuffer reads it in Init and says so here,
  // so the engine re-inits it when the setting changes.
  virtual bool uses_buffer_quality() const { return false; }
  void set_buffer_quality(uint8_t q) {
    buffer_quality_ = q < kNumBufferQualities ? q : 0;
  }
  uint8_t buffer_quality() const { return buffer_quality_; }

 protected:
  uint8_t init_option_ = 0;
  uint8_t buffer_quality_ = 0;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODE_H_
