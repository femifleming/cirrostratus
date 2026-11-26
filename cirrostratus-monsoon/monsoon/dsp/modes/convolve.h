// Mode 3 — CONVOLVE: convolution; FREEZE / TRIG capture the impulse response.
//
// Uniform partitioned convolution (overlap-save), block 512, FFT 1024.
// Spectra are stored as packed int16 (re, im) pairs in CMSIS order, one
// float scale per partition, so the complex multiply-accumulate uses the
// Cortex-M4's dual 16-bit multiplies (SMUSD / SMUADX).
// The input's frequency-domain delay line doubles as the capture memory: capturing an IR runs the
// inverse FFT of the last ~0.15 s of stored input spectra into a 16-bit
// snapshot, so no separate live buffer is needed. Knob changes rebuild the
// IR spectra from that snapshot, a few partitions per Prepare() call.
//
// Latency: 2 x 512 samples (21 ms). All FFT work happens in Prepare().
//
// Per-mode knobs:
//   BAL  noon = v1.0. What excites the IR: left = only the attacks of the
//        input (a transient detector gates out the sustain), right = only
//        the body (attacks ducked, so hits swell instead of crack). It
//        shapes the input before everything, so captures hear it too.
//   FBK  the engine's feedback loop (the v1.0 FEEDBACK: the IR rings on).
//   RVB  IR smear: the IR is built from itself plus eight decaying echoes
//        of itself (3 -> 97 ms), so captures melt into a dense wash.

#ifndef MONSOON_DSP_MODES_CONVOLVE_H_
#define MONSOON_DSP_MODES_CONVOLVE_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class ConvolveMode : public Mode {
 public:
  static const size_t kBlock = 512;
  static const size_t kFft = 2 * kBlock;
  static const size_t kMaxPartitions = 16;   // up to 8192 samples (0.17 s)

  const char* name() const { return "convolve"; }
  bool Init(Arena* arena) __attribute__((cold));  // runs once: small
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();

  size_t GetPersistentData(PersistentChunk* chunks);
  bool LoadPersistentData(const uint8_t* data, size_t size);

  // Diagnostics for tests.
  size_t partitions() const { return partitions_; }
  size_t ir_length() const { return ir_length_; }
  bool busy() const { return build_state_ != BUILD_IDLE || capture_left_ > 0; }

  // FBK drives the engine's feedback loop (as FEEDBACK did in v1.0).
  float feedback_amount(const Controls& c) const { return c.fbk; }
  uint32_t captures() const { return captures_; }

  struct IrParams {
    float position, size, density, texture, pitch;
  };

 private:
  enum BuildState { BUILD_IDLE, BUILD_PARTITIONS };

  struct Generator {
    size_t n;
    float env;
    float lp, hp_lp;
  };

  void ProcessAudioBlock();
  void CaptureStep();
  void BuildStep(int max_partitions);
  void BuildAll();
  void StartBuild(const IrParams& p);
  void SetupGenerator();
  float Raw(float pos) const;
  float SmearedRaw(size_t n) const;
  float NextIrSample(Generator* g) const;
  void MakeSyntheticRoom();
  void Quantize(const float* spec, uint32_t* dst, float* scale);
  bool ParamsChanged(const IrParams& a, const IrParams& b) const;

  size_t partitions_;
  uint32_t* fdl_[2][kMaxPartitions];  // kFft / 2 packed (re, im) words
  uint32_t* ir_[kMaxPartitions];
  float* fdl_scale_[2];
  float* ir_scale_;
  float* pending_scale_;      // unnormalised scales of the IR being built
  int16_t* snapshot_;
  float* snap_scale_;          // one scale per kBlock of snapshot
  float* in_buf_[2][2];        // [parity][channel] kBlock each
  float* out_buf_[2][2];
  float* prev_[2];
  float* work_;
  float* spec_;               // forward spectrum, then the accumulator

  // ISR side.
  size_t fill_;
  int parity_;
  volatile uint32_t blocks_ready_;
  uint32_t blocks_done_;
  bool prev_freeze_;
  volatile bool capture_request_;
  Controls ctl_;

  // Main-loop side.
  size_t head_;                // next FDL slot to write
  size_t active_partitions_;
  size_t capture_left_;
  size_t capture_head_;
  uint32_t captures_;

  BuildState build_state_;
  IrParams built_params_;
  IrParams pending_params_;
  bool force_build_;
  uint32_t last_build_block_;
  size_t build_partition_;
  Generator gen_;
  float energy_;
  float norm_;
  // Derived generator settings.
  size_t ir_length_;
  float rate_, start_, reverse_, decay_, lp_coef_, hp_coef_, treble_;
  bool lowpass_;
  float smear_;          // RVB the IR is being built with
  float built_smear_;
  float env_fast_;       // BAL transient detector (ISR)
  float env_slow_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CONVOLVE_H_
