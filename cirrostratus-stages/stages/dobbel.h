// Cirrostratus Stages: a Stages mode where every segment is its own instrument.
//
// Six segment types, each with its own flavours:
//   RANDOM     - slewed random voltages, seven kinds, and the weather
//   CHAOS      - patterned randomness: Turing loop, logistic map, Lorenz,
//                cellular automaton, coin-toss gates, dust, stair row
//   ENVELOPE   - attack and decay in one segment, curve, loop, ratchets
//   OSCILLATOR - eight audio oscillators (morph, 16-wave bank, additive,
//                difference pair, folder, FM from the left neighbour,
//                Gendy (a row shares one waveform), wave-rider)
//   PROCESSOR  - CV: envelope follower, transient gate, slew, crush/fold,
//                gesture looper, quantiser (Sleepcousonique's 38 scales),
//                sample/track and hold, comparator
//   EFFECT     - audio: the left neighbour's sound at the full rate through
//                a filter, VCA / low-pass gate, ring modulator, comb,
//                chorus, echo, drive or lo-fi
//
// Cords (after Sleepcousonique's): a segment can take its left neighbour's
// output into its CV input through a hall wire, fog, water, a door, a range
// or a maybe. Dream drift: left alone for a minute, the whole panel starts
// to wander, and comes back at the first touch.
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
  TYPE_EFFECT,
  TYPE_LAST
};

enum RandomFlavour {
  RANDOM_UNIFORM, RANDOM_GAUSS, RANDOM_WALK, RANDOM_PINK, RANDOM_QUANTISED,
  RANDOM_KIN, RANDOM_ORBIT, RANDOM_WEATHER, RANDOM_LAST
};

enum ChaosFlavour {
  CHAOS_TURING, CHAOS_LOGISTIC, CHAOS_LORENZ, CHAOS_CELLULAR, CHAOS_COIN,
  CHAOS_DUST, CHAOS_STAIR, CHAOS_LAST
};

enum EnvelopeFlavour {
  ENVELOPE_ONCE, ENVELOPE_TWICE, ENVELOPE_THRICE, ENVELOPE_FOUR, ENVELOPE_LAST
};

enum OscillatorFlavour {
  OSC_MORPH, OSC_BANK, OSC_ADDITIVE, OSC_PAIR, OSC_FOLD, OSC_FM, OSC_GENDY,
  OSC_RIDER, OSC_LAST
};

enum ProcessorFlavour {
  PROC_FOLLOW, PROC_TRANSIENT, PROC_SLEW, PROC_CRUSH, PROC_LOOPER,
  PROC_QUANTISE, PROC_SAMPLE, PROC_COMPARE, PROC_LAST
};

enum EffectFlavour {
  FX_FILTER, FX_VCA, FX_RING, FX_COMB, FX_CHORUS, FX_ECHO, FX_DRIVE, FX_LOFI,
  FX_LAST
};

// Cords from the left neighbour into a segment's CV input (Sleepcousonique's
// -> ~> => >| range ?>).
enum CordKind {
  CORD_NONE,
  CORD_HALL,   // -> a plain wire
  CORD_FOG,    // ~> softened (8 ms)
  CORD_WATER,  // => a capacitor: slow to fill, slow to drain (60 ms)
  CORD_DOOR,   // >| a diode: only what is above 0 V gets through
  CORD_RANGE,  // range: the neighbour's swing, whatever its polarity, as 0 to amount
  CORD_MAYBE,  // ?> each rise rolls the dice: through, or silent until the next
  CORD_LAST
};

const int kNumScales = 38;

extern const uint8_t kNumFlavours[TYPE_LAST];

struct SegmentConfig {
  uint8_t type;
  uint8_t flavour[TYPE_LAST];  // remembered per type
  uint8_t aux[TYPE_LAST];      // hold + pot, 0-255, remembered per type
  uint8_t alt;                 // long press, one bit per type
  uint8_t cord;                // CordKind, from the left neighbour
  uint8_t cord_amount;         // 0-255 = -1 to +1
};

struct Config {
  SegmentConfig segment[kNumSegments];
  uint8_t version;
  uint8_t drift;               // dream drift on
  uint8_t padding[2];

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
  inline float cord_amount(int i) const {
    return static_cast<float>(segment[i].cord_amount) * (2.0f / 255.0f) - 1.0f;
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
const int kBankLevels = 8;           // band-limited: harmonics 128, 64, ... 1

// The wave bank lives in flash (stages/dobbel_bank.cc, tools/make_bank.py).
extern const int16_t kBankData[];  // each table has t[-1], t[N], t[N + 1]
extern const uint16_t kBankOffset[kBankTables][kBankLevels];
extern const uint8_t kBankBits[kBankTables][kBankLevels];
extern const float kBankGain[kBankTables];

struct Segment {
  float cv_out, cv_norm;   // a CV type's last step (1.3.2: once a block)
  uint8_t type;
  uint8_t flavour;
  bool started;

  // shared
  float out;        // last output (1.0 = 8 V)
  float norm;       // 0-1 for neighbours and LEDs
  float phase;      // clock phase (random / chaos) or oscillator phase
  float phase2;
  float dt;         // oscillator: last block's phase increment (glides)
  float fold_sin;   // oscillator: the folder's last sine
  float fold_z[38]; // oscillator: the folder's 2x oversampled history (twice)
  int fold_pos;
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

  // effect
  float f1, f2;     // filter / VCA states
  float fx_env;
  float fx_phase;
  float fx_held;
  uint16_t fx_write;
  uint16_t fx_count;

  // quantiser, stair
  float q_cents;
  uint8_t step;

  // the cord into this segment
  float cord_s;
  float cord_prev;
  float cord_open;
};

struct Memory {
  int16_t looper[kNumSegments][kLooperLength];
  int16_t rider[kNumSegments][kRiderLength];
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

  void ProcessRandom(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessChaos(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessEnvelope(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessOscillator(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessProcessor(int i, const Config& c, const Input& in, float* out, size_t size);
  void ProcessEffect(int i, const Config& c, const Input& in, float* out, size_t size);
  float Cord(int i, const Config& c);
  void Drift(const Config& c, const Input* input, size_t size);

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

  // Dream drift: how far (0-1), how long nothing was touched, where the
  // dream is, and the controls last seen (to notice a touch).
  size_t size_;      // this block's length
  // Each segment's slider (+ CV, cords, dream) and pot as it last heard
  // them, for the stair row's leader.
  float heard_slider_[kNumSegments];
  float heard_pot_[kNumSegments];
  float drift_;
  uint32_t idle_;
  float dream_x_, dream_y_, dream_z_;
  float last_slider_[kNumSegments];
  float last_pot_[kNumSegments];
  uint32_t last_config_;   // a checksum of the settings: any button gesture changes it
  uint32_t drift_block_;   // counts blocks (the checksum is taken every 8th)
 public:
  inline float drift() const { return drift_; }
 private:

  DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace dobbel
}  // namespace stages

#endif  // STAGES_DOBBEL_H_
