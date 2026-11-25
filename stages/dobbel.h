// Dobbeltrap: a Stages mode where every segment is its own instrument.
//
// Five segment types, each with its own flavours:
//   RANDOM     - slewed random voltages, seven kinds
//   CHAOS      - patterned randomness: Turing loop, logistic map, Lorenz,
//                cellular automaton, coin-toss gates, dust
//   ENVELOPE   - attack and decay in one segment, curve, loop, ratchets
//   OSCILLATOR - eight audio oscillators (morph, 16-wave bank, additive,
//                difference pair, folder, FM from the left neighbour,
//                Gendy, wave-rider)
//   PROCESSOR  - envelope follower, transient gate, slew, crush/fold,
//                gesture looper
//
// The engine knows nothing about the hardware: it takes the controls, gates
// and CV of six segments and returns their outputs, so the host tests run
// exactly this code.

#ifndef STAGES_DOBBEL_H_
#define STAGES_DOBBEL_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/gate_flags.h"

namespace stages {
namespace dobbel {

const int kNumSegments = 6;
const size_t kMaxBlockSize = 8;
const float kSampleRate = 31250.0f;
const float kAudioPeak = 0.625f;  // 5 V (1.0 = 8 V at the DAC)

enum Type {
  TYPE_RANDOM,
  TYPE_CHAOS,
  TYPE_ENVELOPE,
  TYPE_OSCILLATOR,
  TYPE_PROCESSOR,
  TYPE_LAST
};

enum RandomFlavour {
  RANDOM_UNIFORM, RANDOM_GAUSS, RANDOM_WALK, RANDOM_PINK, RANDOM_QUANTISED,
  RANDOM_KIN, RANDOM_ORBIT, RANDOM_LAST
};

enum ChaosFlavour {
  CHAOS_TURING, CHAOS_LOGISTIC, CHAOS_LORENZ, CHAOS_CELLULAR, CHAOS_COIN,
  CHAOS_DUST, CHAOS_LAST
};

enum EnvelopeFlavour {
  ENVELOPE_ONCE, ENVELOPE_TWICE, ENVELOPE_THRICE, ENVELOPE_FOUR, ENVELOPE_LAST
};

enum OscillatorFlavour {
  OSC_MORPH, OSC_BANK, OSC_ADDITIVE, OSC_PAIR, OSC_FOLD, OSC_FM, OSC_GENDY,
  OSC_RIDER, OSC_LAST
};

enum ProcessorFlavour {
  PROC_FOLLOW, PROC_TRANSIENT, PROC_SLEW, PROC_CRUSH, PROC_LOOPER, PROC_LAST
};

extern const uint8_t kNumFlavours[TYPE_LAST];

struct SegmentConfig {
  uint8_t type;
  uint8_t flavour[TYPE_LAST];  // remembered per type
  uint8_t aux[TYPE_LAST];      // hold + pot, 0-255, remembered per type
  uint8_t alt;                 // long press, one bit per type
};

struct Config {
  SegmentConfig segment[kNumSegments];
  uint8_t version;
  uint8_t padding[3];

  enum { tag = 0x4C424F44 };  // DOBL

  void Init();
  void Sanitize();
  inline bool alt(int i) const {
    return (segment[i].alt >> segment[i].type) & 1;
  }
  inline uint8_t flavour(int i) const {
    return segment[i].flavour[segment[i].type];
  }
  inline float aux(int i) const {
    return static_cast<float>(segment[i].aux[segment[i].type]) / 255.0f;
  }
};

struct Input {
  float slider;      // 0-1, the slider alone
  float pot;         // 0-1
  float cv;          // CV jack, filtered (1.0 = 8 V)
  float cv_raw;      // CV jack, unfiltered: audio in for the processors
  float cv_slider;   // slider + CV (-1 to 2)
  bool patched;      // a cable in the gate jack
  const stmlib::GateFlags* gate;
};

// Buffers that need real memory. On the module they live in the memory the
// normal Stages engines use (they are idle in this mode).
const int kLooperLength = 768;       // 12 s at 64 Hz
const int kRiderLength = 256;
const int kBankTables = 16;
const int kBankLength = 256;

struct Segment {
  uint8_t type;
  uint8_t flavour;
  bool started;

  // shared
  float out;        // last output (1.0 = 8 V)
  float norm;       // 0-1 for neighbours and LEDs
  float phase;      // clock phase (random / chaos) or oscillator phase
  float phase2;
  uint32_t since;   // samples since the last tick
  float period;     // samples between ticks
  float target;
  float value;
  float lp;
  uint8_t event;    // a tick happened (for the LED)

  // random
  float walk;
  float pink[5];
  float pink_sum;
  uint32_t counter;

  // chaos
  uint32_t reg;
  float x, y, z;
  uint64_t cells;
  uint32_t gate_samples;

  // envelope
  uint8_t stage;
  uint8_t repeats_left;
  float env;
  float env_from;
  float env_phase;
  bool gate_was_high;

  // oscillator
  float voice;
  float dc;
  float gendy_amp[12];
  float gendy_dur[12];
  float gendy_sum;
  float gendy_inv;  // 1 / length of the breakpoint segment playing
  uint8_t gendy_index;
  float rider_write;

  // processor
  float f_dc;
  float f_env;
  float f_slow;
  uint16_t hold_blocks;
  float held;
  uint16_t rec_length;
  uint16_t loop_length;
  float rec_clock;
  float play;
  bool recording;
  uint16_t refractory;
  bool armed;
};

struct Memory {
  int16_t looper[kNumSegments][kLooperLength];
  int16_t rider[kNumSegments][kRiderLength];
  int16_t bank[kBankTables][kBankLength];
};

class Engine {
 public:
  Engine() { }
  ~Engine() { }

  void Init(Memory* memory);
  void Process(
      const Config& config,
      const Input* input,
      float output[kNumSegments][kMaxBlockSize],
      size_t size);

  inline float level(int i) const { return segment_[i].norm; }
  inline bool take_event(int i) {
    bool e = segment_[i].event != 0;
    segment_[i].event = 0;
    return e;
  }

 private:
  void Reset(int i, uint8_t type, uint8_t flavour);
  void BuildBank();

  void ProcessRandom(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessChaos(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessEnvelope(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessOscillator(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessProcessor(int i, const Config& c, const Input& in, float* out, size_t size);

  bool Tick(Segment* s, const Input& in, float rate_hz, size_t n);
  int RunPosition(int i, const Config& c) const;

  Segment segment_[kNumSegments];
  Memory* memory_;

  // Per-sample values a run of linked segments (Orbit, Lorenz, cellular)
  // shares: written by the leftmost segment of the run, read by the others.
  float run_x_[kNumSegments][kMaxBlockSize];
  float run_y_[kNumSegments][kMaxBlockSize];
  float run_z_[kNumSegments][kMaxBlockSize];
  uint64_t run_cells_[kNumSegments][kMaxBlockSize];
  // Each segment's output this block, for its right neighbour.
  float block_out_[kNumSegments][kMaxBlockSize];
  float block_norm_[kNumSegments][kMaxBlockSize];

  DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace dobbel
}  // namespace stages

#endif  // STAGES_DOBBEL_H_
