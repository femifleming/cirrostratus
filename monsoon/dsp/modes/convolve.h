// Mode 3 — CONVOLVE: convolution; FREEZE / TRIG capture the impulse response.
//
// Uniform partitioned convolution (overlap-save), block 256, FFT 512.
// The input's frequency-domain delay line (int16 spectra, one scale per
// partition) doubles as the capture memory: capturing an IR runs the
// inverse FFT of the last ~0.17 s of stored input spectra into a 16-bit
// snapshot, so no separate live buffer is needed. Knob changes rebuild the
// IR spectra from that snapshot, a few partitions per Prepare() call.
//
// Latency: 2 x 256 samples (10.7 ms). All FFT work happens in Prepare().

#ifndef MONSOON_DSP_MODES_CONVOLVE_H_
#define MONSOON_DSP_MODES_CONVOLVE_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class ConvolveMode : public Mode {
 public:
  static const size_t kBlock = 256;
  static const size_t kFft = 2 * kBlock;
  static const size_t kMaxPartitions = 32;   // 8192-sample IR (0.17 s)

  const char* name() const { return "convolve"; }
  bool Init(Arena* arena);
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n);
  void Prepare();

  size_t GetPersistentData(PersistentChunk* chunks);
  bool LoadPersistentData(const uint8_t* data, size_t size);

  // Diagnostics for tests.
  size_t partitions() const { return partitions_; }
  size_t ir_length() const { return ir_length_; }
  bool busy() const { return build_state_ != BUILD_IDLE || capture_left_ > 0; }
  uint32_t captures() const { return captures_; }

  struct IrParams {
    float position, size, density, texture, pitch;
  };

 private:
  enum BuildState { BUILD_IDLE, BUILD_ENERGY, BUILD_PARTITIONS };

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
  float NextIrSample(Generator* g) const;
  void MakeSyntheticRoom();
  void Quantize(const float* spec, int16_t* dst, float* scale);
  bool ParamsChanged(const IrParams& a, const IrParams& b) const;

  size_t partitions_;
  int16_t* fdl_[2][kMaxPartitions];
  int16_t* ir_[kMaxPartitions];
  float* fdl_scale_[2];
  float* ir_scale_;
  int16_t* snapshot_;
  float* snap_scale_;          // one scale per kBlock of snapshot
  float* in_buf_[2][2];        // [parity][channel] kBlock each
  float* out_buf_[2][2];
  float* prev_[2];
  float* work_;
  float* spec_;
  float* acc_;

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
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_CONVOLVE_H_
