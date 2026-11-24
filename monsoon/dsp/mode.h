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

 protected:
  uint8_t init_option_ = 0;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODE_H_
