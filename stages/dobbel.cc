// Dobbeltrap engine. See dobbel.h.

#include "stages/dobbel.h"

#include <cmath>
#include <cstring>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"
#include "stages/resources.h"

namespace stages {
namespace dobbel {

using namespace stmlib;

const uint8_t kNumFlavours[TYPE_LAST] = {
  RANDOM_LAST, CHAOS_LAST, ENVELOPE_LAST, OSC_LAST, PROC_LAST
};

// ---------------------------------------------------------------------------
// Configuration

void Config::Init() {
  memset(this, 0, sizeof(*this));
  version = 1;
  for (int i = 0; i < kNumSegments; ++i) {
    SegmentConfig& s = segment[i];
    // The user's usual layout: four randoms, an envelope, an oscillator.
    s.type = i < 4 ? TYPE_RANDOM : (i == 4 ? TYPE_ENVELOPE : TYPE_OSCILLATOR);
    s.aux[TYPE_RANDOM] = 255;      // full amplitude
    s.aux[TYPE_CHAOS] = 255;
    s.aux[TYPE_ENVELOPE] = 128;    // linear curve
    s.aux[TYPE_OSCILLATOR] = 128;  // fine tune centred
    s.aux[TYPE_PROCESSOR] = 0;     // gain x1
  }
}

void Config::Sanitize() {
  if (version != 1) {
    Init();
    return;
  }
  for (int i = 0; i < kNumSegments; ++i) {
    SegmentConfig& s = segment[i];
    if (s.type >= TYPE_LAST) s.type = TYPE_RANDOM;
    for (int t = 0; t < TYPE_LAST; ++t) {
      if (s.flavour[t] >= kNumFlavours[t]) s.flavour[t] = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Small helpers

namespace {

inline float Clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

inline float Wrap(float x) {
  return x - floorf(x);
}

// sin(2 pi p) and cos(2 pi p) for p in [0, 1)
inline float Sin(float p) {
  return Interpolate(lut_sine, p, 1024.0f);
}

inline float Cos(float p) {
  return Interpolate(lut_sine, p + 0.25f, 1024.0f);
}

// lo * (hi / lo) ^ x
inline float ExpMap(float x, float lo, float hi) {
  return lo * expf(x * logf(hi / lo));
}

inline float Gauss() {
  return (Random::GetFloat() + Random::GetFloat() + Random::GetFloat() +
          Random::GetFloat()) * 0.25f;
}

inline float Blep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  } else if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

// Reflects x back into [-1, 1] as many times as needed.
inline float Fold(float x) {
  float u = (x + 1.0f) * 0.25f;
  u -= floorf(u);
  return 1.0f - 4.0f * fabsf(u - 0.5f);
}

// Envelope curve: x in [0, 1], shape 0 (exponential, fast start) to 1
// (logarithmic, slow start); 0.5 is linear.
inline float Curve(float x, float shape) {
  float k = expf((shape - 0.5f) * 5.0f);
  return x / (x + k * (1.0f - x));
}

inline float OnePoleCoefficient(float seconds) {
  if (seconds <= 0.0f) return 1.0f;
  return 1.0f - expf(-1.0f / (seconds * kSampleRate));
}

const uint8_t kCellularRules[8] = { 30, 90, 110, 150, 184, 45, 73, 54 };

}  // namespace

// ---------------------------------------------------------------------------
// Engine

void Engine::Init(Memory* memory) {
  memory_ = memory;
  memset(memory_, 0, sizeof(Memory));
  memset(segment_, 0, sizeof(segment_));
  for (int i = 0; i < kNumSegments; ++i) {
    segment_[i].type = 0xff;  // forces a reset on the first block
  }
  memset(block_out_, 0, sizeof(block_out_));
  memset(block_norm_, 0, sizeof(block_norm_));
  BuildBank();
}

// Sixteen waveforms in the spirit of the SDS VCO's bank: the classics, then
// shapes that only a table can make.
void Engine::BuildBank() {
  float smooth_random[17];
  uint32_t seed = Random::state();
  Random::Seed(0x5d5d5d5d);
  for (int k = 0; k < 16; ++k) smooth_random[k] = Random::GetFloat() * 2.0f - 1.0f;
  smooth_random[16] = smooth_random[0];
  Random::Seed(seed);

  float w[kBankLength];
  for (int t = 0; t < kBankTables; ++t) {
    float peak = 0.0f;
    float mean = 0.0f;
    for (int n = 0; n < kBankLength; ++n) {
      const float p = static_cast<float>(n) / kBankLength;
      const float s = Sin(p);
      float v = 0.0f;
      switch (t) {
        case 0: v = s; break;                                         // sine
        case 1: v = 1.0f - 4.0f * fabsf(p - 0.5f) ; v = -v; break;     // triangle
        case 2: v = 2.0f * p - 1.0f; break;                           // saw
        case 3: v = p < 0.5f ? 1.0f : -1.0f; break;                   // square
        case 4: v = p < 0.25f ? 1.0f : -1.0f; break;                  // pulse 25 %
        case 5: v = p < 0.1f ? 1.0f : -1.0f; break;                   // pulse 10 %
        case 6: v = s > 0.0f ? s : 0.0f; break;                       // half sine
        case 7: v = fabsf(s); break;                                  // full-wave sine
        case 8: v = s * s * s; break;                                 // pinched sine
        case 9: v = floorf(p * 4.0f) / 1.5f - 1.0f; break;            // 4 steps
        case 10: v = floorf(p * 8.0f) / 3.5f - 1.0f; break;           // 8 steps
        case 11: v = Sin(Wrap(p * 5.0f)) * (0.5f - 0.5f * Cos(p)); break;  // formant
        case 12: v = Wrap(p * 2.7f) * 2.0f - 1.0f; break;             // sync saw
        case 13: {                                                    // frozen noise
            const float x = p * 16.0f;
            const int k = static_cast<int>(x);
            const float f = x - k;
            const float e = f * f * (3.0f - 2.0f * f);
            v = smooth_random[k] + (smooth_random[k + 1] - smooth_random[k]) * e;
          }
          break;
        case 14: v = s + 0.5f * Sin(Wrap(p * 2.0f)); break;           // sine + octave
        case 15:                                                      // organ
          v = s + 0.6f * Sin(Wrap(p * 2.0f)) + 0.4f * Sin(Wrap(p * 4.0f)) +
              0.25f * Sin(Wrap(p * 3.0f));
          break;
      }
      w[n] = v;
      mean += v;
    }
    mean /= kBankLength;
    for (int n = 0; n < kBankLength; ++n) {
      w[n] -= mean;
      if (fabsf(w[n]) > peak) peak = fabsf(w[n]);
    }
    const float scale = peak > 0.0f ? 32000.0f / peak : 0.0f;
    for (int n = 0; n < kBankLength; ++n) {
      memory_->bank[t][n] = static_cast<int16_t>(w[n] * scale);
    }
  }
}

void Engine::Reset(int i, uint8_t type, uint8_t flavour) {
  Segment& s = segment_[i];
  memset(&s, 0, sizeof(s));
  s.type = type;
  s.flavour = flavour;
  s.period = kSampleRate;
  s.target = s.value = s.walk = Random::GetFloat();
  s.reg = Random::GetWord();
  s.x = 0.1f + Random::GetFloat() * 0.1f;
  s.y = 0.0f;
  s.z = 0.0f;
  if (type == TYPE_CHAOS && flavour == CHAOS_LOGISTIC) {
    s.x = 0.2f + Random::GetFloat() * 0.6f;
  }
  s.cells = 1ULL << 3;
  for (int k = 0; k < 5; ++k) {
    s.pink[k] = Random::GetFloat();
    s.pink_sum += s.pink[k];
  }
  for (int k = 0; k < 12; ++k) {
    s.gendy_amp[k] = Sin(static_cast<float>(k) / 12.0f);
    s.gendy_dur[k] = 1.0f;
  }
  s.gendy_sum = 12.0f;
  s.armed = true;  // the transient gate is ready for the first hit
  if (type == TYPE_OSCILLATOR && flavour == OSC_RIDER) {
    for (int n = 0; n < kRiderLength; ++n) {
      memory_->rider[i][n] = static_cast<int16_t>(
          Sin(static_cast<float>(n) / kRiderLength) * 16000.0f);
    }
  }
  if (type == TYPE_PROCESSOR && flavour == PROC_LOOPER) {
    s.loop_length = 0;
  }
}

// Clock for the random and chaos types: the gate when patched, otherwise the
// rate. Returns true on a tick.
bool Engine::Tick(Segment* s, const Input& in, float rate_hz, size_t n) {
  ++s->since;
  if (in.patched) {
    if (in.gate[n] & GATE_FLAG_RISING) {
      s->period = static_cast<float>(s->since);
      s->since = 0;
      return true;
    }
    return false;
  }
  s->phase += rate_hz / kSampleRate;
  if (s->phase >= 1.0f) {
    s->phase -= floorf(s->phase);
    s->period = kSampleRate / rate_hz;
    s->since = 0;
    return true;
  }
  return false;
}

// How far into a run of identical neighbours (same type and flavour) a
// segment sits: 0 = leader.
int Engine::RunPosition(int i, const Config& c) const {
  int position = 0;
  while (i > 0 &&
         c.segment[i - 1].type == c.segment[i].type &&
         c.flavour(i - 1) == c.flavour(i)) {
    ++position;
    --i;
  }
  return position;
}

void Engine::Process(
    const Config& config,
    const Input* input,
    float output[kNumSegments][kMaxBlockSize],
    size_t size) {
  for (int i = 0; i < kNumSegments; ++i) {
    const uint8_t type = config.segment[i].type;
    const uint8_t flavour = config.flavour(i);
    if (segment_[i].type != type || segment_[i].flavour != flavour) {
      Reset(i, type, flavour);
    }
    float* out = output[i];
    switch (type) {
      case TYPE_RANDOM: ProcessRandom(i, config, input[i], out, size); break;
      case TYPE_CHAOS: ProcessChaos(i, config, input[i], out, size); break;
      case TYPE_ENVELOPE: ProcessEnvelope(i, config, input[i], out, size); break;
      case TYPE_OSCILLATOR: ProcessOscillator(i, config, input[i], out, size); break;
      default: ProcessProcessor(i, config, input[i], out, size); break;
    }
    for (size_t n = 0; n < size; ++n) {
      block_out_[i][n] = out[n];
    }
    segment_[i].out = out[size - 1];
  }
}

// ---------------------------------------------------------------------------
// RANDOM

void Engine::ProcessRandom(
    int i, const Config& c, const Input& in, float* out, size_t size) {
  Segment& s = segment_[i];
  const float a = Clamp(in.cv_slider, -0.5f, 1.5f);
  const float rate = ExpMap(a, 0.05f, 100.0f);
  const float chance = Clamp(in.cv_slider, 0.0f, 1.0f);
  const float b = in.pot;
  const float aux = c.aux(i);
  const bool bipolar = c.alt(i);
  const int run = RunPosition(i, c);

  for (size_t n = 0; n < size; ++n) {
    if (s.flavour == RANDOM_ORBIT) {
      if (run == 0) {
        // The leader moves a point along a path; the run reads X, Y, radius.
        s.phase2 += rate * 0.25f / kSampleRate;
        if (s.phase2 >= 1.0f) {
          s.phase2 -= 1.0f;
          s.event = 1;
        }
        const float t = s.phase2;
        // the wandering path moves on its own clock (gate or rate)
        if (Tick(&s, in, rate, n)) {
          s.target = Random::GetFloat() * 2.0f - 1.0f;
          s.walk = Random::GetFloat() * 2.0f - 1.0f;
        }
        s.x += (s.target - s.x) * 0.0005f;
        s.y += (s.walk - s.y) * 0.0005f;
        float x, y;
        const float shape = b * 3.0f;
        const int k = static_cast<int>(shape);
        const float f = shape - k;
        float px[2], py[2];
        for (int j = 0; j < 2; ++j) {
          switch (k + j > 3 ? 3 : k + j) {
            case 0: px[j] = Cos(t); py[j] = Sin(t); break;                 // circle
            case 1: px[j] = Sin(t); py[j] = Sin(Wrap(t * 2.0f)) * 0.7f; break;  // figure 8
            case 2: {                                                       // spiral
                const float r = 0.25f + 0.75f * fabsf(Sin(Wrap(t * 0.125f)));
                px[j] = Cos(Wrap(t * 3.0f)) * r;
                py[j] = Sin(Wrap(t * 3.0f)) * r;
              }
              break;
            default:                                                        // wander
              px[j] = s.x;
              py[j] = s.y;
              break;
          }
        }
        x = px[0] + (px[1] - px[0]) * f;
        y = py[0] + (py[1] - py[0]) * f;
        run_x_[i][n] = x;
        run_y_[i][n] = y;
        run_z_[i][n] = sqrtf(x * x + y * y);
        s.value = x * 0.5f + 0.5f;
      } else {
        const int leader = i - run;
        const float v = run % 3 == 1 ? run_y_[leader][n]
            : (run % 3 == 2 ? run_z_[leader][n] * 2.0f - 1.0f : -run_x_[leader][n]);
        s.value = Clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);
      }
      s.lp = s.value;
    } else {
      bool tick = Tick(&s, in, rate, n);
      if (tick && in.patched && Random::GetFloat() > chance) {
        tick = false;  // clocked: the slider is the chance of a new value
      }
      if (tick) {
        s.event = 1;
        switch (s.flavour) {
          case RANDOM_UNIFORM:
            s.target = Random::GetFloat();
            break;
          case RANDOM_GAUSS:
            s.target = Clamp(0.5f + (Gauss() - 0.5f) * 1.7f, 0.0f, 1.0f);
            break;
          case RANDOM_WALK:
            // Xenakis: a random walk between two reflecting barriers.
            s.walk += (Gauss() - 0.5f) * 0.5f;
            if (s.walk > 1.0f) s.walk = 2.0f - s.walk;
            if (s.walk < 0.0f) s.walk = -s.walk;
            s.target = s.walk;
            break;
          case RANDOM_PINK: {
              // Voss-McCartney: row k changes every 2^k ticks.
              ++s.counter;
              uint32_t bits = s.counter;
              int k = 0;
              while (k < 4 && !(bits & 1)) {
                bits >>= 1;
                ++k;
              }
              s.pink_sum -= s.pink[k];
              s.pink[k] = Random::GetFloat();
              s.pink_sum += s.pink[k];
              s.target = s.pink_sum * 0.2f;
            }
            break;
          case RANDOM_QUANTISED: {
              // Semitones over 1-5 octaves (aux); the output is in V/oct.
              const int octaves = 1 + static_cast<int>(aux * 4.0f + 0.5f);
              const int semis = static_cast<int>(
                  Random::GetFloat() * (12 * octaves + 1));
              s.target = static_cast<float>(semis) / (12.0f * 5.0f);
            }
            break;
          case RANDOM_KIN: {
              // Stays near the left neighbour; the pot is how loosely.
              const float left = i > 0 ? block_norm_[i - 1][n] : 0.5f;
              s.target = Clamp(left + (Random::GetFloat() - 0.5f) * b, 0.0f, 1.0f);
            }
            break;
        }
      }
      // slew: the pot, as a fraction of the time between new values
      const float slew_b = s.flavour == RANDOM_KIN ? 0.2f : b;
      const float tau = slew_b * slew_b * s.period;
      if (tau < 0.5f) {
        s.lp = s.target;  // no slew: a clean step
      } else {
        s.lp += (s.target - s.lp) / (1.0f + tau);
      }
      s.value = s.lp;
    }

    float v = s.value;
    float o;
    if (s.flavour == RANDOM_QUANTISED) {
      o = v * 5.0f / 8.0f;  // 0 to 5 V, one octave per volt
      if (bipolar) o -= 2.5f / 8.0f;
    } else if (bipolar) {
      o = (v * 2.0f - 1.0f) * kAudioPeak * aux;
    } else {
      o = v * aux;
    }
    out[n] = o;
    block_norm_[i][n] = v;
  }
  s.norm = s.value;
}

// ---------------------------------------------------------------------------
// CHAOS

void Engine::ProcessChaos(
    int i, const Config& c, const Input& in, float* out, size_t size) {
  Segment& s = segment_[i];
  const float a = Clamp(in.cv_slider, -0.5f, 1.5f);
  const float rate = ExpMap(a, 0.05f, 100.0f);
  const float b = in.pot;
  const float aux = c.aux(i);
  const bool bipolar = c.alt(i);
  const int run = RunPosition(i, c);
  const int leader = i - run;

  for (size_t n = 0; n < size; ++n) {
    float v = s.value;
    bool gate_out = false;
    switch (s.flavour) {
      case CHAOS_TURING: {
          if (Tick(&s, in, rate, n)) {
            s.event = 1;
            // pot: locked loop (left), random (middle), locked and inverted
            // every pass (right, a loop twice as long)
            const float p = 1.0f - fabsf(b * 2.0f - 1.0f);
            uint32_t bit = (s.reg >> 7) & 1;
            if (Random::GetFloat() < p) {
              bit = Random::GetWord() >> 31;
            } else if (b > 0.5f) {
              bit ^= 1;
            }
            s.reg = ((s.reg << 1) | bit) & 0xff;
            s.target = static_cast<float>(s.reg) / 255.0f;
          }
          s.lp += (s.target - s.lp) * 0.02f;
          v = s.lp;
        }
        break;

      case CHAOS_LOGISTIC:
        if (Tick(&s, in, rate, n)) {
          s.event = 1;
          const float r = 3.4f + 0.6f * b;
          s.x = r * s.x * (1.0f - s.x);
          if (s.x <= 0.0f || s.x >= 1.0f) s.x = 0.3f + Random::GetFloat() * 0.4f;
          s.target = s.x;
        }
        s.lp += (s.target - s.lp) * 0.02f;
        v = s.lp;
        break;

      case CHAOS_LORENZ:
        if (run == 0) {
          const float rho = 20.0f + b * 30.0f;
          const float h = rate * 2e-5f;  // Euler step: slider = speed
          float x = s.x, y = s.y, z = s.z;
          const float dx = 10.0f * (y - x);
          const float dy = x * (rho - z) - y;
          const float dz = x * y - (8.0f / 3.0f) * z;
          x += dx * h;
          y += dy * h;
          z += dz * h;
          if (!(fabsf(x) < 200.0f && fabsf(y) < 200.0f && fabsf(z) < 200.0f)) {
            x = 0.1f; y = 0.0f; z = 0.0f;
          }
          s.x = x; s.y = y; s.z = z;
          run_x_[i][n] = Clamp((x + 25.0f) / 50.0f, 0.0f, 1.0f);
          run_y_[i][n] = Clamp((y + 30.0f) / 60.0f, 0.0f, 1.0f);
          run_z_[i][n] = Clamp(z / 60.0f, 0.0f, 1.0f);
          v = run_x_[i][n];
        } else {
          const int axis = run % 3;
          v = axis == 1 ? run_y_[leader][n]
              : (axis == 2 ? run_z_[leader][n] : 1.0f - run_x_[leader][n]);
        }
        break;

      case CHAOS_CELLULAR:
        if (run == 0) {
          // Ring of 8 cells per segment of the run.
          int length = 1;
          while (i + length < kNumSegments &&
                 c.segment[i + length].type == TYPE_CHAOS &&
                 c.flavour(i + length) == CHAOS_CELLULAR) {
            ++length;
          }
          const int cells = 8 * length;
          if (Tick(&s, in, rate, n)) {
            s.event = 1;
            const uint8_t rule = kCellularRules[static_cast<int>(b * 7.99f)];
            uint64_t next = 0;
            for (int k = 0; k < cells; ++k) {
              const int l = (k + cells - 1) % cells;
              const int r = (k + 1) % cells;
              const int pattern = (((s.cells >> l) & 1) << 2) |
                                  (((s.cells >> k) & 1) << 1) |
                                  ((s.cells >> r) & 1);
              if ((rule >> pattern) & 1) next |= 1ULL << k;
            }
            if (next == 0 || next == s.cells) {
              next ^= 1ULL << (Random::GetWord() % cells);  // never stuck
            }
            s.cells = next;
          }
          run_cells_[i][n] = s.cells;
          v = static_cast<float>(s.cells & 0xff) / 255.0f;
        } else {
          v = static_cast<float>((run_cells_[leader][n] >> (8 * run)) & 0xff) / 255.0f;
        }
        break;

      case CHAOS_COIN:
        // A coin toss per clock: heads opens the gate for half a period.
        if (Tick(&s, in, rate, n)) {
          if (Random::GetFloat() < b) {
            s.event = 1;
            s.gate_samples = static_cast<uint32_t>(s.period * 0.5f);
            if (s.gate_samples < 150) s.gate_samples = 150;
          }
        }
        gate_out = true;
        v = s.gate_samples ? 1.0f : 0.0f;
        if (s.gate_samples) --s.gate_samples;
        break;

      case CHAOS_DUST: {
          // Triggers at random moments, no clock. Slider = density.
          const float density = ExpMap(Clamp(in.cv_slider, 0.0f, 1.2f), 0.1f, 50.0f);
          if (Random::GetFloat() < density / kSampleRate) {
            s.event = 1;
            s.gate_samples = 160;  // 5 ms
            s.target = 1.0f - b * Random::GetFloat();
          }
          gate_out = true;
          v = s.gate_samples ? s.target : 0.0f;
          if (s.gate_samples) --s.gate_samples;
        }
        break;
    }
    s.value = v;
    float o;
    if (bipolar && !gate_out) {
      o = (v * 2.0f - 1.0f) * kAudioPeak * aux;
    } else {
      o = v * aux;
    }
    out[n] = o;
    block_norm_[i][n] = v;
  }
  s.norm = s.value;
}

// ---------------------------------------------------------------------------
// ENVELOPE

enum EnvelopeStage { ENV_IDLE, ENV_ATTACK, ENV_SUSTAIN, ENV_DECAY };

void Engine::ProcessEnvelope(
    int i, const Config& c, const Input& in, float* out, size_t size) {
  Segment& s = segment_[i];
  // CV jack: both times, 1 V = half as long.
  const float scale = exp2f(-in.cv * 8.0f);
  const float attack = ExpMap(Clamp(in.slider, 0.0f, 1.0f), 0.001f, 10.0f) * scale;
  const float decay = ExpMap(Clamp(in.pot, 0.0f, 1.0f), 0.001f, 10.0f) * scale;
  const float attack_inc = 1.0f / (attack * kSampleRate);
  const float decay_inc = 1.0f / (decay * kSampleRate);
  const float shape = c.aux(i);
  const bool loop = c.alt(i);
  const int repeats = s.flavour;  // 0 = once, 1-3 = extra hits

  for (size_t n = 0; n < size; ++n) {
    const bool high = in.patched && (in.gate[n] & GATE_FLAG_HIGH);
    const bool rising = in.patched && (in.gate[n] & GATE_FLAG_RISING);
    if (rising || (!in.patched && loop && s.stage == ENV_IDLE)) {
      s.stage = ENV_ATTACK;
      s.env_from = s.env;
      s.env_phase = 0.0f;
      s.repeats_left = repeats;
      s.event = 1;
    }
    switch (s.stage) {
      case ENV_ATTACK:
        s.env_phase += attack_inc;
        if (s.env_phase >= 1.0f) {
          s.env_phase = 0.0f;
          s.env = 1.0f;
          // A gate still held after the attack sustains; a trigger decays.
          s.stage = (high && !loop && repeats == 0) ? ENV_SUSTAIN : ENV_DECAY;
        } else {
          s.env = s.env_from + (1.0f - s.env_from) * Curve(s.env_phase, shape);
        }
        break;
      case ENV_SUSTAIN:
        s.env = 1.0f;
        if (!high) {
          s.stage = ENV_DECAY;
          s.env_phase = 0.0f;
        }
        break;
      case ENV_DECAY:
        s.env_phase += decay_inc;
        if (s.env_phase >= 1.0f) {
          s.env = 0.0f;
          s.env_phase = 0.0f;
          if (s.repeats_left) {
            --s.repeats_left;
            s.stage = ENV_ATTACK;
            s.env_from = 0.0f;
            s.event = 1;
          } else if (loop && (!in.patched || high)) {
            s.stage = ENV_ATTACK;
            s.env_from = 0.0f;
            s.event = 1;
          } else {
            s.stage = ENV_IDLE;
          }
        } else {
          s.env = 1.0f - Curve(s.env_phase, shape);
        }
        break;
      default:
        break;
    }
    out[n] = s.env;
    block_norm_[i][n] = s.env;
  }
  s.norm = s.env;
}

// ---------------------------------------------------------------------------
// OSCILLATOR

void Engine::ProcessOscillator(
    int i, const Config& c, const Input& in, float* out, size_t size) {
  Segment& s = segment_[i];
  // Slider: C1 to C7. CV: 1 V/oct. Hold + pot: fine tune, +/- 1 semitone.
  const float note = 24.0f + Clamp(in.slider, 0.0f, 1.0f) * 72.0f +
      in.cv * 96.0f + (c.aux(i) - 0.5f) * 2.0f;
  float f = 440.0f * exp2f((note - 69.0f) / 12.0f);
  f = Clamp(f, 0.5f, 12000.0f);
  const float dt = f / kSampleRate;
  const float b = Clamp(in.pot, 0.0f, 1.0f);
  const bool voice = c.alt(i);
  const float attack_coef = OnePoleCoefficient(0.002f);
  const float release_coef = OnePoleCoefficient(0.3f);

  for (size_t n = 0; n < size; ++n) {
    // The gate: hard sync, or in voice mode the built-in VCA's envelope.
    if (in.patched && (in.gate[n] & GATE_FLAG_RISING) && !voice) {
      s.phase = 0.0f;
    }
    if (voice) {
      const bool open = !in.patched || (in.gate[n] & GATE_FLAG_HIGH);
      if (in.patched && (in.gate[n] & GATE_FLAG_RISING)) s.event = 1;
      s.voice += ((open ? 1.0f : 0.0f) - s.voice) * (open ? attack_coef : release_coef);
    } else {
      s.voice = 1.0f;
    }

    float p = s.phase;
    float y = 0.0f;
    bool already_scaled = false;
    switch (s.flavour) {
      case OSC_MORPH: {
          // sine -> triangle -> saw -> square -> narrow pulse
          const float m = b * 3.999f;
          const int k = static_cast<int>(m);
          const float frac = m - k;
          float w[2];
          for (int j = 0; j < 2; ++j) {
            switch (k + j) {
              case 0: w[j] = Sin(p); break;
              case 1: w[j] = 1.0f - 4.0f * fabsf(Wrap(p + 0.25f) - 0.5f); break;
              case 2: w[j] = 2.0f * p - 1.0f - Blep(p, dt); break;
              case 3: {
                  const float q = Wrap(p + 0.5f);
                  w[j] = (p < 0.5f ? 1.0f : -1.0f) + Blep(p, dt) - Blep(q, dt);
                }
                break;
              default: {
                  const float pw = 0.5f - 0.42f * frac;
                  const float q = Wrap(p + 1.0f - pw);
                  w[j] = (p < pw ? 1.0f : -1.0f) + Blep(p, dt) - Blep(q, dt);
                }
                break;
            }
          }
          // the last quarter narrows the square's pulse instead of fading
          y = k < 3 ? w[0] + (w[1] - w[0]) * frac : w[1];
        }
        break;

      case OSC_BANK: {
          // Sixteen waveforms; the pot scans them, crossfading neighbours.
          const float m = b * (kBankTables - 1.001f);
          const int k = static_cast<int>(m);
          const float frac = m - k;
          const float x = p * kBankLength;
          const int j = static_cast<int>(x);
          const float fx = x - j;
          const int j2 = (j + 1) & (kBankLength - 1);
          const int16_t* t0 = memory_->bank[k];
          const int16_t* t1 = memory_->bank[k + 1];
          const float a0 = t0[j] + (t0[j2] - t0[j]) * fx;
          const float a1 = t1[j] + (t1[j2] - t1[j]) * fx;
          y = (a0 + (a1 - a0) * frac) / 32000.0f;
        }
        break;

      case OSC_ADDITIVE: {
          // 1 to 16 harmonics at 1/k, none above 14 kHz.
          const float harmonics = 1.0f + b * 15.0f;
          const float sn = Sin(p);
          const float cs = Cos(p);
          float s_prev = 0.0f;
          float s_k = sn;
          float sum = 0.0f;
          for (int k = 1; k <= 16; ++k) {
            if (k * f > 14000.0f) break;
            const float weight = k <= harmonics ? 1.0f
                : (k - 1 < harmonics ? harmonics - (k - 1) : 0.0f);
            if (weight <= 0.0f) break;
            sum += s_k * weight / k;
            const float s_next = 2.0f * cs * s_k - s_prev;
            s_prev = s_k;
            s_k = s_next;
          }
          y = sum * 0.6f;
        }
        break;

      case OSC_PAIR: {
          // Two sines; between them the ear hears a third, lower tone.
          const float ratio = 1.0f + b * 0.5f;
          s.phase2 += dt * ratio;
          if (s.phase2 >= 1.0f) s.phase2 -= 1.0f;
          y = 0.5f * (Sin(p) + Sin(s.phase2));
        }
        break;

      case OSC_FOLD:
        y = Fold(Sin(p) * (1.0f + b * 6.0f));
        break;

      case OSC_FM: {
          // The left neighbour is the modulator (an oscillator for FM, a
          // random or envelope for slow phase bending); pot = depth.
          const float m = i > 0 ? block_out_[i - 1][n] / kAudioPeak : 0.0f;
          y = Sin(Wrap(p + b * 2.0f * m));
        }
        break;

      case OSC_GENDY: {
          // Xenakis' dynamic stochastic synthesis: 12 breakpoints whose
          // heights and spacing take a random walk; pot = how far.
          const int k = s.gendy_index;
          const int k2 = (k + 1) % 12;
          s.phase2 += dt * s.gendy_sum / s.gendy_dur[k];
          if (s.phase2 >= 1.0f) {
            s.phase2 -= 1.0f;
            s.gendy_index = k2;
            const float step = b * 0.35f;
            float a = s.gendy_amp[k2] + (Gauss() - 0.5f) * 2.0f * step;
            if (a > 1.0f) a = 2.0f - a;
            if (a < -1.0f) a = -2.0f - a;
            s.gendy_amp[k2] = a;
            float d = s.gendy_dur[k2] + (Gauss() - 0.5f) * 2.0f * step;
            d = Clamp(d, 0.4f, 2.5f);
            s.gendy_sum += d - s.gendy_dur[k2];
            s.gendy_dur[k2] = d;
            if (k2 == 0) s.event = 1;
          }
          const int a = s.gendy_index;
          const int a2 = (a + 1) % 12;
          y = s.gendy_amp[a] + (s.gendy_amp[a2] - s.gendy_amp[a]) * s.phase2;
          y -= s.dc;
          s.dc += y * 0.0005f;
        }
        break;

      case OSC_RIDER: {
          // Writes the left neighbour (segment 1: its own CV jack) into its
          // waveform, one full table every 0.05-10 s (pot); gate = freeze.
          const bool freeze = in.patched && (in.gate[n] & GATE_FLAG_HIGH);
          if (!freeze) {
            const float source = i > 0 ? block_out_[i - 1][n] : in.cv_raw;
            const float seconds = ExpMap(b, 0.05f, 10.0f);
            const int w = static_cast<int>(s.rider_write);
            memory_->rider[i][w] = static_cast<int16_t>(
                Clamp(source, -1.0f, 1.0f) * 32767.0f);
            s.rider_write += kRiderLength / (seconds * kSampleRate);
            if (s.rider_write >= kRiderLength) {
              s.rider_write -= kRiderLength;
              s.event = 1;
            }
          }
          const float x = p * kRiderLength;
          const int j = static_cast<int>(x);
          const float fx = x - j;
          const int16_t* t = memory_->rider[i];
          const float v = (t[j] + (t[(j + 1) & (kRiderLength - 1)] - t[j]) * fx) / 32767.0f;
          s.dc += (v - s.dc) * 0.0002f;
          y = Clamp(v - s.dc, -1.0f, 1.0f);
          already_scaled = true;  // already in output units (1.0 = 8 V)
        }
        break;
    }

    s.phase += dt;
    if (s.phase >= 1.0f) s.phase -= 1.0f;
    const float o = (already_scaled ? y : y * kAudioPeak) * s.voice;
    out[n] = o;
    block_norm_[i][n] = 0.5f + 0.5f * Clamp(o / kAudioPeak, -1.0f, 1.0f);
  }
  s.norm = voice ? s.voice : 1.0f;
}

// ---------------------------------------------------------------------------
// PROCESSOR

void Engine::ProcessProcessor(
    int i, const Config& c, const Input& in, float* out, size_t size) {
  Segment& s = segment_[i];
  const float slider = Clamp(in.slider, 0.0f, 1.0f);
  const float b = Clamp(in.pot, 0.0f, 1.0f);
  const float aux = c.aux(i);
  const bool alt = c.alt(i);
  const float x = in.cv_raw;  // read once per block: lo-fi, by design

  switch (s.flavour) {
    case PROC_FOLLOW: {
        // The BRIBESTAGES follower: slider = release, pot = attack,
        // hold + pot = gain x1-x8, long press = invert, gate = freeze.
        const float gain = 1.0f + aux * 7.0f;
        const float release = OnePoleCoefficient(ExpMap(slider, 0.005f, 5.0f));
        const float attack = OnePoleCoefficient(b * 0.25f);
        for (size_t n = 0; n < size; ++n) {
          s.f_dc += (x - s.f_dc) * 0.001f;
          const float rect = fabsf(x - s.f_dc) * gain / kAudioPeak;
          const bool freeze = in.patched && (in.gate[n] & GATE_FLAG_HIGH);
          if (!freeze) {
            s.f_env += (rect - s.f_env) * (rect > s.f_env ? attack : release);
          }
          const float e = Clamp(s.f_env, 0.0f, 1.0f);
          out[n] = alt ? -e : e;
          block_norm_[i][n] = e;
        }
        s.norm = Clamp(s.f_env, 0.0f, 1.0f);
      }
      break;

    case PROC_TRANSIENT: {
        // A 5 V trigger when the input jumps: slider = threshold,
        // pot = trigger length 2-100 ms, hold + pot = input gain.
        const float gain = 1.0f + aux * 7.0f;
        const float threshold = 0.02f + slider * slider * 0.6f;
        const uint32_t length = static_cast<uint32_t>(
            ExpMap(b, 0.002f, 0.1f) * kSampleRate);
        const float fast_up = OnePoleCoefficient(0.0005f);
        const float fast_down = OnePoleCoefficient(0.02f);
        const float slow = OnePoleCoefficient(0.1f);
        for (size_t n = 0; n < size; ++n) {
          s.f_dc += (x - s.f_dc) * 0.001f;
          const float rect = fabsf(x - s.f_dc) * gain / kAudioPeak;
          s.f_env += (rect - s.f_env) * (rect > s.f_env ? fast_up : fast_down);
          s.f_slow += (rect - s.f_slow) * slow;
          if (s.refractory) --s.refractory;
          // re-arm only once the hit has died down, so one hit = one trigger
          if (!s.armed && !s.refractory && s.f_env < s.f_slow * 1.2f + threshold * 0.5f) {
            s.armed = true;
          }
          if (s.armed && s.f_env > s.f_slow * 1.5f + threshold) {
            s.gate_samples = length;
            s.refractory = 1500;  // 48 ms at least between triggers
            s.armed = false;
            s.event = 1;
          }
          const float v = s.gate_samples ? 1.0f : 0.0f;
          if (s.gate_samples) --s.gate_samples;
          out[n] = alt ? (1.0f - v) * kAudioPeak : v * kAudioPeak;
          block_norm_[i][n] = v;
        }
        s.norm = Clamp(s.f_env, 0.0f, 1.0f);
      }
      break;

    case PROC_SLEW: {
        // Lag the CV: slider = rise, pot = fall (0-5 s); gate = hold.
        const float up = OnePoleCoefficient(slider * slider * 5.0f);
        const float down = OnePoleCoefficient(b * b * 5.0f);
        for (size_t n = 0; n < size; ++n) {
          const bool hold = in.patched && (in.gate[n] & GATE_FLAG_HIGH);
          if (!hold) {
            s.lp += (in.cv - s.lp) * (in.cv > s.lp ? up : down);
          }
          out[n] = alt ? -s.lp : s.lp;
          block_norm_[i][n] = Clamp(fabsf(s.lp), 0.0f, 1.0f);
        }
        s.norm = Clamp(fabsf(s.lp), 0.0f, 1.0f);
      }
      break;

    case PROC_CRUSH: {
        // Audio in, folded (slider = drive) and held for 1-32 blocks
        // (pot), so the rate drops. Hold + pot = output level; gate = freeze.
        const int hold = 1 + static_cast<int>(b * 31.0f);
        const bool freeze = in.patched && (in.gate[0] & GATE_FLAG_HIGH);
        if (!freeze) {
          if (++s.hold_blocks >= hold) {
            s.hold_blocks = 0;
            s.f_dc += (x - s.f_dc) * 0.01f;
            s.held = Fold((x - s.f_dc) / kAudioPeak * (1.0f + slider * 7.0f));
          }
        }
        const float level = 0.25f + aux * 1.75f;
        const float o = (alt ? -s.held : s.held) * kAudioPeak * Clamp(level, 0.0f, 2.0f);
        for (size_t n = 0; n < size; ++n) {
          out[n] = o;
          block_norm_[i][n] = 0.5f + 0.5f * s.held;
        }
        s.norm = fabsf(s.held);
      }
      break;

    default: {  // PROC_LOOPER
        // Gate high records the CV jack (64 times a second, up to 12 s);
        // gate low loops it. Slider = speed 1/4x-4x (noon 1x), pot = smoothing,
        // long press = backwards.
        const float speed = exp2f((slider - 0.5f) * 4.0f);
        const float smooth = OnePoleCoefficient(b * b * 0.5f);
        int16_t* buffer = memory_->looper[i];
        for (size_t n = 0; n < size; ++n) {
          const bool high = in.patched && (in.gate[n] & GATE_FLAG_HIGH);
          if (in.patched && (in.gate[n] & GATE_FLAG_RISING)) {
            s.recording = true;
            s.rec_length = 0;
            s.rec_clock = 0.0f;
            s.event = 1;
          }
          if (s.recording) {
            s.rec_clock += 64.0f / kSampleRate;
            if (s.rec_clock >= 1.0f) {
              s.rec_clock -= 1.0f;
              if (s.rec_length < kLooperLength) {
                buffer[s.rec_length++] = static_cast<int16_t>(
                    Clamp(in.cv, -1.0f, 1.0f) * 32767.0f);
              }
            }
            if (!high || s.rec_length >= kLooperLength) {
              s.recording = false;
              s.loop_length = s.rec_length > 1 ? s.rec_length : 0;
              s.play = 0.0f;
            }
          }
          float v;
          if (s.recording || s.loop_length == 0) {
            v = in.cv;  // nothing to loop yet: pass the CV through
          } else {
            const float len = static_cast<float>(s.loop_length);
            s.play += (alt ? -speed : speed) * 64.0f / kSampleRate;
            while (s.play >= len) s.play -= len;
            while (s.play < 0.0f) s.play += len;
            const int j = static_cast<int>(s.play);
            const float fx = s.play - j;
            const int j2 = (j + 1) % s.loop_length;
            v = (buffer[j] + (buffer[j2] - buffer[j]) * fx) / 32767.0f;
            if (j == 0 && fx < 64.0f / kSampleRate * speed) s.event = 1;
          }
          s.lp += (v - s.lp) * smooth;
          out[n] = s.lp;
          block_norm_[i][n] = Clamp(fabsf(s.lp), 0.0f, 1.0f);
        }
        s.norm = Clamp(fabsf(s.lp), 0.0f, 1.0f);
      }
      break;
  }
}

}  // namespace dobbel
}  // namespace stages
