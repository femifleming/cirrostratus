// Mode 15 — FALTUNG: convolution with a built-in impulse-response library.
//
// | Control  | Function                                                    |
// |----------|-------------------------------------------------------------|
// | POSITION | Which IR (8 zones): room, hall, plate, spring, cabinet,     |
// |          | wooden body, metal sheet, cathedral. 30 ms crossfade.        |
// | SIZE     | IR length (fully CW = the whole IR; shorter also saves CPU) |
// | DENSITY  | Pre-delay 0 - 200 ms                                        |
// | TEXTURE  | Tilt EQ on the wet signal, dark <-> bright (+/- 6 dB)       |
// | PITCH    | Wet filter: CCW from noon = high-cut (20 kHz -> 1.5 kHz),   |
// |          | CW from noon = low-cut (20 Hz -> 800 Hz). Noon = full range.|
// | FREEZE   | Input muted; the wet signal loops back through the IR at    |
// |          | the level it had when FREEZE was pressed (endless).         |
// | TRIG     | Next IR (adds one to the IR chosen by POSITION)             |
// | Options  | none                                                        |
//
// Per-mode knobs:
//   BAL  early / late: noon = the IR as it is. Left: the IR's tail is faded
//        down (up to -36 dB at its end): close, dry, intimate. Right: the
//        early part is faded down and the tail up (+48 dB across the IR):
//        a far-away space that swells in. The level is kept constant.
//   FBK  feedback through the IR: the left wet goes back into the
//        convolution, so the space builds on itself (a 170 ms room rings
//        several times longer, denser). The loop gain stays below 0.95 at
//        the IR's peak frequency, so it never howls; FREEZE is the endless
//        version.
//   RVB  the engine's FDN reverb: a long smooth tail (0.5 -> 16 s) behind
//        the IR, which is at most 170 ms (the IR is the early sound, the
//        FDN the late one).
//
// The IRs are synthesised by tools/gen_irs.py and stored in flash as int16
// partition spectra (faltung_irs.cc, ~135 KB), so they cost no RAM. Input
// is the mono sum; each IR is stereo. Uniform partitioned convolution,
// block 256 / FFT 512, all FFT work in Prepare(). Latency 512 samples.

#ifndef MONSOON_DSP_MODES_FALTUNG_H_
#define MONSOON_DSP_MODES_FALTUNG_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class FaltungMode : public Mode {
 public:
  static const size_t kBlock = 256;
  static const size_t kFft = 2 * kBlock;
  static const size_t kMaxPartitions = 32;
  static const size_t kMaxPredelay = 9600;  // 200 ms

  const char* name() const { return "faltung"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();
  float reverb_amount(const Controls& c) const { return c.rvb; }

  // Diagnostics for tests.
  int current_ir() const { return ir_index_; }
  size_t active_partitions() const { return active_partitions_; }

 private:
  void ProcessAudioBlock();
  void Accumulate(int ir, size_t parts, int ch, float* acc,
                  const float* weight);
  void BalWeights(int ir, size_t parts, float bal, float* weight);

  // Prepare side.
  int16_t* fdl_[kMaxPartitions];
  float* fdl_scale_;
  float* in_buf_[2];          // [parity], mono, kBlock
  float* out_buf_[2][2];      // [parity][channel]
  float* prev_;
  float* work_;
  float* spec_;
  float* acc_;
  int16_t* predelay_;
  size_t head_;

  // ISR side.
  size_t fill_;
  int parity_;
  volatile uint32_t blocks_ready_;
  uint32_t blocks_done_;
  size_t pd_write_;
  float pd_delay_;

  volatile int ir_index_;        // IR the next block uses
  int playing_ir_;               // IR used by the last block
  int fade_from_;                // IR being faded out, -1 = none
  int fade_blocks_;              // blocks left in the crossfade
  volatile size_t active_partitions_;
  int trig_offset_;
  bool prev_trigger_;

  // Wet filters / freeze loop.
  float tilt_lp_[2];
  float tilt_coef_;
  float gain_lo_, gain_hi_;
  float filt_ic1_[2], filt_ic2_[2];
  float filt_g_, filt_k_;
  int filt_mode_;                // 0 off, 1 low-pass, 2 high-pass
  bool frozen_;
  float freeze_target_;
  float freeze_gain_;
  float wet_power_;
  float fb_sample_;

  // Knobs.
  float* part_energy_;           // [ir][partition], both channels
  volatile float bal_;
  volatile float bal_peak_;      // largest BAL weight (bounds FBK)
  float fb_gain_;                // FBK, smoothed per block
  float fb_left_;                // FBK: last left wet sample
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_FALTUNG_H_
