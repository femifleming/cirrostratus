// Cirrostratus Stages engine. See dobbel.h.

#include "stages/dobbel.h"

#include <cmath>
#include <cstring>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"
#include "stages/resources.h"

// Code size: the module's flash is nearly full, so loops are not unrolled
// here, except the short per-sample ones of the oscillators, the effects and
// Engine::Process (HOT_LOOP: 8 samples a block, unrolled whole). The wave-bank, additive,
// Gendy, rider, echo and lo-fi loops measured as fast or faster rolled.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("no-unroll-loops")
#endif
#define HOT_LOOP _Pragma("GCC unroll 8")

namespace stages {
namespace dobbel {

using namespace stmlib;

const uint8_t kNumFlavours[TYPE_LAST] = {
  RANDOM_LAST, CHAOS_LAST, ENVELOPE_LAST, OSC_LAST, PROC_LAST, FX_LAST
};

// ---------------------------------------------------------------------------
// Configuration

void Config::Init() {
  memset(this, 0, sizeof(*this));
  version = 2;
  for (int i = 0; i < kNumSegments; ++i) {
    SegmentConfig& s = segment[i];
    // The user's usual layout: four randoms, an envelope, an oscillator.
    s.type = i < 4 ? TYPE_RANDOM : (i == 4 ? TYPE_ENVELOPE : TYPE_OSCILLATOR);
    s.aux[TYPE_RANDOM] = 255;      // full amplitude
    s.aux[TYPE_CHAOS] = 255;
    s.aux[TYPE_ENVELOPE] = 128;    // linear curve
    s.aux[TYPE_OSCILLATOR] = 128;  // fine tune centred
    s.aux[TYPE_PROCESSOR] = 0;     // gain x1
    s.aux[TYPE_EFFECT] = 128;
    s.cord = CORD_NONE;
    s.cord_amount = 255;           // a new cord: full, upright
  }
}

void Config::Sanitize() {
  if (version != 2) {  // (1.0's settings are another size: they start fresh)
    Init();
    return;
  }
  if (drift > 1) drift = 0;
  for (int i = 0; i < kNumSegments; ++i) {
    SegmentConfig& s = segment[i];
    if (s.type >= TYPE_LAST) s.type = TYPE_RANDOM;
    for (int t = 0; t < TYPE_LAST; ++t) {
      if (s.flavour[t] >= kNumFlavours[t]) s.flavour[t] = 0;
    }
    if (s.cord >= CORD_LAST) s.cord = CORD_NONE;
  }
}

// ---------------------------------------------------------------------------
// Small helpers

namespace {

__attribute__((always_inline)) inline float Clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// No libm in the real-time paths: the newlib wrappers need errno (not linked
// in this firmware) and cost far too many cycles per sample.
inline float Floor(float x) {
  const int i = static_cast<int>(x);
  return static_cast<float>(x < static_cast<float>(i) ? i - 1 : i);
}

inline float Wrap(float x) {
  return x - Floor(x);
}

// 2^x, about 1e-4 relative error (0.15 cent).
inline float Exp2(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 126.0f) x = 126.0f;
  const float fi = Floor(x);
  const float f = x - fi;
  const float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (
      0.0555041f + f * (0.0096181f + f * 0.0013334f))));
  union { float f; int32_t i; } u;
  u.f = p;
  u.i += static_cast<int32_t>(fi) * 8388608;  // add fi to the exponent
  return u.f;
}

// log2 for the constant ranges below (evaluated once per call site value).
inline float Log2(float x) {
  union { float f; int32_t i; } u;
  u.f = x;
  const float e = static_cast<float>(((u.i >> 23) & 0xff) - 127);
  u.i = (u.i & 0x007fffff) | 0x3f800000;  // mantissa in [1, 2)
  const float m = u.f;
  // log2(m) on [1, 2): good to ~1e-4
  return e + (m - 1.0f) * (1.4425449f + (m - 1.0f) * (-0.7181452f + (m - 1.0f) * (
      0.4575485f + (m - 1.0f) * (-0.2779042f + (m - 1.0f) * (0.1217970f + (m - 1.0f) *
      -0.0258411f)))));
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
  return lo * Exp2(x * Log2(hi / lo));
}

inline float Gauss() {
  return (Random::GetFloat() + Random::GetFloat() + Random::GetFloat() +
          Random::GetFloat()) * 0.25f;
}

// The levels for a phase increment dt: level L holds harmonics up to
// 128 >> L, so it is safe while (128 >> L) * dt <= 0.5. Crossfading the
// first safe level with the next one down keeps the brightness continuous
// as the pitch moves, and nothing folds back below Nyquist.
struct Mip {
  int lo, hi;
  float w;  // how much of the brighter level (lo)
};

inline Mip BankMip(float dt) {
  Mip m;
  const float x = Log2(dt * 256.0f);
  if (x <= 0.0f) {
    m.lo = m.hi = 0;
    m.w = 1.0f;
    return m;
  }
  int k = static_cast<int>(x);
  if (static_cast<float>(k) < x) ++k;
  if (k >= kBankLevels - 1) {
    m.lo = m.hi = kBankLevels - 1;
    m.w = 1.0f;
    return m;
  }
  m.lo = k;
  m.hi = k + 1;
  m.w = static_cast<float>(k) - x;
  return m;
}

// The oscillators' phase (since 1.3.3) is a 32-bit fraction of a turn: it
// wraps by itself, an offset (a quarter turn, half a turn) is one integer
// add, and a table index is one shift.
const float kInvTurn = 1.0f / 4294967296.0f;

inline uint32_t Turns(float x) {  // x in [0, 1] (1 itself: just short of a turn)
  return static_cast<uint32_t>(x * 4294967040.0f);
}

// sin(2 pi ph / 2^32) from the 1024-point table
inline float SinU(uint32_t ph) {
  const uint32_t j = ph >> 22;
  const float f = static_cast<float>(ph << 10) * kInvTurn;
  const float a = lut_sine[j];
  return a + (lut_sine[j + 1] - a) * f;
}

// One wave of the bank at two mip levels, looked up once per block.
struct BankWave {
  const int16_t* lo;
  const int16_t* hi;
  int bits_lo, bits_hi;
  int32_t w;  // how much of lo, 0-128
};

inline BankWave GetBankWave(int wave, const Mip& m) {
  BankWave b;
  b.lo = kBankData + kBankOffset[wave][m.lo];
  b.hi = kBankData + kBankOffset[wave][m.hi];
  b.bits_lo = kBankBits[wave][m.lo];
  b.bits_hi = kBankBits[wave][m.hi];
  b.w = static_cast<int32_t>(m.w * 128.0f + 0.5f);
  return b;
}

// Linear read of a 2^bits table, in integers (one conversion to float per
// read, not two); the guard point after the table means no wrapping. In
// table units times 2^14.
inline int32_t ReadTable(const int16_t* t, int bits, uint32_t ph) {
  const uint32_t j = ph >> (32 - bits);
  const int32_t f = static_cast<int32_t>((ph << bits) >> 18);  // 14 bits
  const int32_t x0 = t[j];
  return x0 * 16384 + (t[j + 1] - x0) * f;
}

const float kTableUnit = 1.0f / 16384.0f;  // ReadBank() to table units

// In table units times 2^14 (peak about 32000 * 2^14). kTwo: crossfading two
// mip levels (the pitch is in the darker part of an octave); decided once
// per block.
template<bool kTwo>
inline float ReadBank(const BankWave& b, uint32_t ph) {
  const int32_t a = ReadTable(b.lo, b.bits_lo, ph);
  if (!kTwo) return static_cast<float>(a);
  const int32_t c = ReadTable(b.hi, b.bits_hi, ph);
  return static_cast<float>(c + ((a - c) >> 7) * b.w);
}

// Morph: sine -> triangle -> saw -> square -> narrow pulse, all read from
// the band-limited bank (square and pulse are two saws apart). Each quarter
// of the knob is a crossfade of two shapes, the weights worked out per block.
template<bool kTwo>
void RenderMorph(int k, float frac, const BankWave& tri, const BankWave& saw,
                 float tri_gain, float saw_gain, float g,
                 const uint32_t* phase, float* out, size_t size) {
  const uint32_t kQuarter = 0x40000000u;
  tri_gain *= kTableUnit;
  saw_gain *= kTableUnit;
  switch (k) {
    case 0: {
        const float a = (1.0f - frac) * g;
        const float t = -frac * tri_gain * g;
        for (size_t n = 0; n < size; ++n) {
          const uint32_t p = phase[n];
          out[n] = SinU(p) * a + ReadBank<kTwo>(tri, p + kQuarter) * t;
        }
      }
      break;
    case 1: {
        const float t = -(1.0f - frac) * tri_gain * g;
        const float s = frac * saw_gain * g;
        for (size_t n = 0; n < size; ++n) {
          const uint32_t p = phase[n];
          out[n] = ReadBank<kTwo>(tri, p + kQuarter) * t + ReadBank<kTwo>(saw, p) * s;
        }
      }
      break;
    case 2: {
        // saw + (square - saw) * frac, square = shifted - saw
        const float s = (1.0f - 2.0f * frac) * saw_gain * g;
        const float h = frac * saw_gain * g;
        for (size_t n = 0; n < size; ++n) {
          const uint32_t p = phase[n];
          out[n] = ReadBank<kTwo>(saw, p) * s + ReadBank<kTwo>(saw, p + 2 * kQuarter) * h;
        }
      }
      break;
    default: {
        // the last quarter narrows the square's pulse
        const float pw = 0.5f - 0.42f * frac;
        const uint32_t offset = Turns(1.0f - pw);
        const float dc = (2.0f * pw - 1.0f) * g;
        const float s = saw_gain * g;
        for (size_t n = 0; n < size; ++n) {
          const uint32_t p = phase[n];
          out[n] = (ReadBank<kTwo>(saw, p + offset) - ReadBank<kTwo>(saw, p)) * s + dc;
        }
      }
      break;
  }
}

// Bank: sixteen waveforms; the pot scans them, crossfading neighbours.
template<bool kTwo>
void RenderBank(const BankWave& a, const BankWave& b, float frac, float g,
                const uint32_t* phase, float* out, size_t size) {
  if (frac > 0.0f) {
    const float ga = (1.0f - frac) * g;
    const float gb = frac * g;
    for (size_t n = 0; n < size; ++n) {
      out[n] = ReadBank<kTwo>(a, phase[n]) * ga + ReadBank<kTwo>(b, phase[n]) * gb;
    }
  } else {
    for (size_t n = 0; n < size; ++n) out[n] = ReadBank<kTwo>(a, phase[n]) * g;
  }
}

// Reflects x back into [-1, 1] as many times as needed. (Biased by 16
// whole periods so u stays positive and truncation is floor: |x| < 63.)
inline float Fold(float x) {
  float u = x * 0.25f + 16.25f;
  u -= static_cast<float>(static_cast<int32_t>(u));
  return 1.0f - 4.0f * fabsf(u - 0.5f);
}



// Envelope curve: x in [0, 1], shape 0 (exponential, fast start) to 1
// (logarithmic, slow start); 0.5 is linear.
// k = CurveK(shape), worked out once per block.
inline float CurveK(float shape) {
  return Exp2((shape - 0.5f) * 7.2134752f);  // e^((shape - 0.5) * 5)
}

inline float Curve(float x, float k) {
  return x / (x + k * (1.0f - x));
}

// The time base. Oscillators and effects run every sample; since 1.3.2 the
// CV types (random, chaos, envelope, processor) run once per block, one
// "step" standing for g_step samples, and glide between blocks. Everything
// they time goes through these: rates, counts, one-pole coefficients.
float g_step = 1.0f;
uint32_t g_step_i = 1;
// Set by a CV type when this block's value should jump, not glide: notes,
// gates, triggers and clean steps.
bool g_jump = false;

inline float StepRate() { return kSampleRate / g_step; }          // steps/s
inline uint32_t Steps(uint32_t samples) {                          // a count
  const uint32_t n = (samples + g_step_i / 2) / g_step_i;
  return n ? n : 1;
}

inline float OnePoleCoefficient(float seconds) {
  if (seconds <= 0.0f) return 1.0f;
  // 1 - e^-z; for small z a series keeps the precision 1 - Exp2() would lose
  const float z = 1.0f / (seconds * StepRate());
  if (z < 0.25f) {
    return z * (1.0f - z * (0.5f - z * (0.16666667f - z * 0.041666667f)));
  }
  return 1.0f - Exp2(-1.4426950f * z);
}

const float kInvSampleRate = 1.0f / kSampleRate;

inline float StepTime() { return kInvSampleRate * g_step; }       // s/step

// A per-sample one-pole coefficient c, for one step: 1 - (1 - c)^step.
inline float PerStep(float c) {
  if (g_step_i == 1) return c;
  return 1.0f - Exp2(g_step * Log2(1.0f - c));
}
const float kInvAudioPeak = 1.0f / kAudioPeak;
const float kSilence[kMaxBlockSize] = { 0.0f };  // block_out_ before the first block
const float kInv255 = 1.0f / 255.0f;
const float kInv32000 = 1.0f / 32000.0f;
const float kInv32767 = 1.0f / 32767.0f;
const float kHarmonicWeight[17] = {
  0.0f, 1.0f, 1.0f / 2, 1.0f / 3, 1.0f / 4, 1.0f / 5, 1.0f / 6, 1.0f / 7,
  1.0f / 8, 1.0f / 9, 1.0f / 10, 1.0f / 11, 1.0f / 12, 1.0f / 13, 1.0f / 14,
  1.0f / 15, 1.0f / 16
};

const uint8_t kCellularRules[8] = { 30, 90, 110, 150, 184, 45, 73, 54 };

// The quantiser's scales: Sleepcousonique's 38 (engine/scq_scales.inc), the
// same order. Each: the period in cents, then n degrees in cents; a negative
// n is n equal steps of the period.
const float kScaleData[] = {
	1200.0000f, 7, 0.000f, 203.910f, 386.314f, 498.045f, 701.955f, 884.359f, 1088.269f, /* 1 just major */
	1200.0000f, 7, 0.000f, 203.910f, 315.641f, 498.045f, 701.955f, 813.686f, 1017.596f, /* 2 just minor */
	1200.0000f, 5, 0.000f, 203.910f, 386.314f, 701.955f, 884.359f, /* 3 just pentatonic */
	1200.0000f, 8, 0.000f, 203.910f, 386.314f, 551.318f, 701.955f, 840.528f, 968.826f, 1088.269f, /* 4 otonal 8-15 */
	1200.0000f, 8, 0.000f, 111.731f, 231.174f, 359.472f, 498.045f, 648.682f, 813.686f, 996.090f, /* 5 utonal */
	1200.0000f, -12, /* 6 chromatic */
	1200.0000f, 7, 0.000f, 200.000f, 400.000f, 500.000f, 700.000f, 900.000f, 1100.000f, /* 7 major */
	1200.0000f, 7, 0.000f, 200.000f, 300.000f, 500.000f, 700.000f, 800.000f, 1000.000f, /* 8 minor */
	1200.0000f, 7, 0.000f, 200.000f, 300.000f, 500.000f, 700.000f, 900.000f, 1000.000f, /* 9 dorian */
	1200.0000f, 7, 0.000f, 100.000f, 300.000f, 500.000f, 700.000f, 800.000f, 1000.000f, /* 10 phrygian */
	1200.0000f, 7, 0.000f, 200.000f, 400.000f, 600.000f, 700.000f, 900.000f, 1100.000f, /* 11 lydian */
	1200.0000f, 7, 0.000f, 200.000f, 400.000f, 500.000f, 700.000f, 900.000f, 1000.000f, /* 12 mixolydian */
	1200.0000f, 7, 0.000f, 100.000f, 300.000f, 500.000f, 600.000f, 800.000f, 1000.000f, /* 13 locrian */
	1200.0000f, 7, 0.000f, 200.000f, 300.000f, 500.000f, 700.000f, 800.000f, 1100.000f, /* 14 harmonic minor */
	1200.0000f, 7, 0.000f, 200.000f, 300.000f, 500.000f, 700.000f, 900.000f, 1100.000f, /* 15 melodic minor */
	1200.0000f, 5, 0.000f, 200.000f, 400.000f, 700.000f, 900.000f, /* 16 major pentatonic */
	1200.0000f, 5, 0.000f, 300.000f, 500.000f, 700.000f, 1000.000f, /* 17 minor pentatonic */
	1200.0000f, 6, 0.000f, 300.000f, 500.000f, 600.000f, 700.000f, 1000.000f, /* 18 blues */
	1200.0000f, -6, /* 19 whole tone */
	1200.0000f, 8, 0.000f, 200.000f, 300.000f, 500.000f, 600.000f, 800.000f, 900.000f, 1100.000f, /* 20 diminished */
	1200.0000f, -24, /* 21 24-TET quarter tones */
	1200.0000f, -19, /* 22 19-TET */
	1200.0000f, -22, /* 23 22-TET */
	1200.0000f, -31, /* 24 31-TET */
	1200.0000f, -53, /* 25 53-TET */
	1901.9550f, -13, /* 26 Bohlen-Pierce (13 steps of 3:1) */
	78.0000f, -1, /* 27 Carlos alpha (78 c steps) */
	63.8000f, -1, /* 28 Carlos beta (63.8 c) */
	35.1000f, -1, /* 29 Carlos gamma (35.1 c) */
	1200.0000f, 5, 0.000f, 231.000f, 474.000f, 717.000f, 955.000f, /* 30 slendro */
	1200.0000f, 7, 0.000f, 120.000f, 270.000f, 540.000f, 670.000f, 785.000f, 950.000f, /* 31 pelog */
	1200.0000f, 7, 0.000f, 200.000f, 350.000f, 500.000f, 700.000f, 900.000f, 1050.000f, /* 32 maqam rast */
	1200.0000f, 7, 0.000f, 150.000f, 300.000f, 500.000f, 700.000f, 800.000f, 1000.000f, /* 33 maqam bayati */
	1200.0000f, 43, 0.000f, 21.506f, 53.273f, 84.467f, 111.731f, 150.637f, 165.004f, 182.404f, 203.910f, 231.174f, 266.871f, 294.135f, 315.641f, 347.408f, 386.314f, 417.508f, 435.084f, 470.781f, 498.045f, 519.551f, 551.318f, 582.512f, 617.488f, 648.682f, 680.449f, 701.955f, 729.219f, 764.916f, 782.492f, 813.686f, 852.592f, 884.359f, 905.865f, 933.129f, 968.826f, 996.090f, 1017.596f, 1034.996f, 1049.363f, 1088.269f, 1115.533f, 1146.727f, 1178.494f, /* 34 Partch 43 */
	1200.0000f, 12, 0.000f, 111.731f, 203.910f, 315.641f, 386.314f, 498.045f, 590.224f, 701.955f, 813.686f, 884.359f, 1017.596f, 1088.269f, /* 35 just chromatic */
	1200.0000f, 12, 0.000f, 90.225f, 203.910f, 294.135f, 407.820f, 498.045f, 611.730f, 701.955f, 792.180f, 905.865f, 996.090f, 1109.775f, /* 36 Pythagorean */
	1200.0000f, 12, 0.000f, 76.000f, 193.200f, 310.300f, 386.300f, 503.400f, 579.500f, 696.600f, 772.600f, 889.700f, 1006.800f, 1082.900f, /* 37 quarter-comma meantone */
	1200.0000f, 12, 0.000f, 90.200f, 192.200f, 294.100f, 390.200f, 498.000f, 588.300f, 696.100f, 792.200f, 888.300f, 996.100f, 1092.200f, /* 38 Werckmeister III */
};

// Finds scale `index` in kScaleData.
const float* FindScale(int index) {
  const float* p = kScaleData;
  for (int k = 0; k < index; ++k) {
    const int n = static_cast<int>(p[1]);
    p += 2 + (n > 0 ? n : 0);
  }
  return p;
}

// The degree of `scale` nearest to `cents` (any octave), in cents.
float NearestDegree(const float* scale, float cents) {
  const float period = scale[0];
  const int n = static_cast<int>(scale[1]);
  const float o = Floor(cents / period);
  const float within = cents - o * period;
  float best = 0.0f, best_d = 1e9f;
  if (n < 0) {
    const float step = period / static_cast<float>(-n);
    const float k = Floor(within / step + 0.5f);
    return o * period + k * step;
  }
  for (int k = 0; k <= n; ++k) {
    const float d = k < n ? scale[2 + k] : period;   // (the next period's root)
    const float dist = fabsf(within - d);
    if (dist < best_d) { best_d = dist; best = d; }
  }
  return o * period + best;
}

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
  for (int i = 0; i < kNumSegments; ++i) block_out_[i] = kSilence;
  memset(block_norm_, 0, sizeof(block_norm_));
  size_ = kMaxBlockSize;
  memset(heard_slider_, 0, sizeof(heard_slider_));
  memset(heard_pot_, 0, sizeof(heard_pot_));
  drift_ = 0.0f;
  idle_ = 0;
  dream_x_ = 1.0f;
  dream_y_ = 1.0f;
  dream_z_ = 20.0f;
  for (int i = 0; i < kNumSegments; ++i) {
    last_slider_[i] = last_pot_[i] = -1.0f;
  }
  last_config_ = 0;
  drift_block_ = 0;
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
  s.gendy_inv = 1.0f;
  s.armed = true;  // the transient gate is ready for the first hit
  if (type == TYPE_RANDOM && flavour == RANDOM_QUANTISED) {
    s.target = s.value = s.lp = 0.0f;  // start on a note, not between two
  }
  if (type == TYPE_OSCILLATOR && flavour == OSC_RIDER) {
    for (int n = 0; n < kRiderLength; ++n) {
      memory_->rider[i][n] = static_cast<int16_t>(
          Sin(static_cast<float>(n) / kRiderLength) * 16000.0f);
    }
  }
  if (type == TYPE_PROCESSOR && flavour == PROC_LOOPER) {
    s.loop_length = 0;
  }
  if (type == TYPE_EFFECT) {
    // comb, chorus and echo use this segment's looper memory
    memset(memory_->looper[i], 0, sizeof(memory_->looper[i]));
  }
  s.cord_open = 1.0f;
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
  s->phase += rate_hz * StepTime();
  if (s->phase >= 1.0f) {
    s->phase -= Floor(s->phase);
    s->period = StepRate() / rate_hz;
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

// The cord into segment i from its left neighbour (segment 1 hears segment
// 6: the row is a ring), once per block, in output units (1.0 = 8 V): what a
// patch cable from that output into this CV jack would add.
float Engine::Cord(int i, const Config& c) {
  const uint8_t kind = c.segment[i].cord;
  if (kind == CORD_NONE) return 0.0f;
  Segment& s = segment_[i];
  const int src = i > 0 ? i - 1 : kNumSegments - 1;
  // segment 6's output is a block old when segment 1 runs (it runs last)
  const float x = segment_[src].out;
  const float amount = c.cord_amount(i);
  const float block_rate = kSampleRate / kMaxBlockSize;
  float v = 0.0f;
  switch (kind) {
    case CORD_HALL:
      v = x * amount;
      break;
    case CORD_FOG:
      s.cord_s += (x * amount - s.cord_s) * (1.0f / (0.008f * block_rate));
      v = s.cord_s;
      break;
    case CORD_WATER:
      s.cord_s += (x * amount - s.cord_s) * (1.0f / (0.06f * block_rate));
      v = s.cord_s;
      break;
    case CORD_DOOR:
      v = (x > 0.0f ? x : 0.0f) * amount;
      break;
    case CORD_RANGE:
      v = block_norm_[src][size_ - 1] * amount;
      break;
    case CORD_MAYBE: {
        // each rise through the middle of the neighbour's swing rolls again
        const float m = block_norm_[src][size_ - 1];
        if (s.cord_prev <= 0.5f && m > 0.5f) {
          s.cord_open = Random::GetFloat() < 0.5f ? 1.0f : 0.0f;
        }
        s.cord_prev = m;
        v = x * amount * s.cord_open;
      }
      break;
  }
  return Clamp(v, -1.0f, 1.0f);
}

// Dream drift. Every touch of a slider, a knob or a button wakes the panel;
// a minute after the last, the controls start to wander on a slow Lorenz
// attractor (each segment its own mix of its three axes), coming in over 30
// s, up to a sixth of their travel. A touch brings them back in half a
// second.
void Engine::Drift(const Config& c, const Input* input, size_t size) {
  bool touched = false;
  for (int i = 0; i < kNumSegments; ++i) {
    if (fabsf(input[i].slider - last_slider_[i]) > 0.02f ||
        fabsf(input[i].pot - last_pot_[i]) > 0.02f) {
      touched = true;
      last_slider_[i] = input[i].slider;
      last_pot_[i] = input[i].pot;
    }
  }
  // FNV-1a over the settings, every 8th block (2 ms) to save the CPU
  if ((++drift_block_ & 7) == 0) {
    uint32_t sum = 2166136261u;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(c.segment);
    for (size_t k = 0; k < sizeof(c.segment); ++k) sum = (sum ^ bytes[k]) * 16777619u;
    if (sum != last_config_) {
      touched = true;
      last_config_ = sum;
    }
  }
  const float block = static_cast<float>(size);
  if (touched || !c.drift) idle_ = 0;
  else if (idle_ < 0xf0000000u) idle_ += size;
  if (c.drift && idle_ > static_cast<uint32_t>(60.0f * kSampleRate)) {
    drift_ += block / (30.0f * kSampleRate);
    if (drift_ > 1.0f) drift_ = 1.0f;
  } else {   // awake: back in half a second
    drift_ -= drift_ * block / (0.15f * kSampleRate);
    if (drift_ < 1e-4f) drift_ = 0.0f;
  }
  if (drift_ > 0.0f) {
    // a slow Lorenz: one loop every ten seconds or so
    const float h = block * (0.25f / kSampleRate);
    const float dx = 10.0f * (dream_y_ - dream_x_);
    const float dy = dream_x_ * (28.0f - dream_z_) - dream_y_;
    const float dz = dream_x_ * dream_y_ - (8.0f / 3.0f) * dream_z_;
    dream_x_ += dx * h;
    dream_y_ += dy * h;
    dream_z_ += dz * h;
    if (!(fabsf(dream_x_) < 100.0f && fabsf(dream_y_) < 100.0f && fabsf(dream_z_) < 100.0f)) {
      dream_x_ = 1.0f; dream_y_ = 1.0f; dream_z_ = 20.0f;
    }
  }
}

void Engine::Process(
    const Config& config,
    const Input* input,
    float output[kNumSegments][kMaxBlockSize],
    size_t) {
  // Always a full block (the module's blocks are 8 samples; stages.cc checks
  // it): a constant length lets the compiler unroll the loops.
  const size_t size = kMaxBlockSize;
  size_ = size;
  for (int i = 0; i < kNumSegments; ++i) {   // (the cooked values come below)
    heard_slider_[i] = input[i].cv_slider;
    heard_pot_[i] = input[i].pot;
  }
  Drift(config, input, size);
  const float ax = dream_x_ * (1.0f / 20.0f);
  const float ay = dream_y_ * (1.0f / 27.0f);
  const float az = (dream_z_ - 25.0f) * (1.0f / 20.0f);
  for (int i = 0; i < kNumSegments; ++i) {
    const uint8_t type = config.segment[i].type;
    const uint8_t flavour = config.flavour(i);
    if (segment_[i].type != type || segment_[i].flavour != flavour) {
      Reset(i, type, flavour);
    }
    // what this segment hears: its own controls, the cord from the left,
    // and the dream
    Input in = input[i];
    const float cord = Cord(i, config);
    if (cord != 0.0f) {
      in.cv += cord;
      in.cv_raw += cord;
      in.cv_slider += cord;
    }
    if (drift_ > 0.0f) {
      static const float kMix[kNumSegments][3] = {
        { 1.0f, 0.3f, 0.0f }, { -0.4f, 1.0f, 0.2f }, { 0.2f, -0.5f, 1.0f },
        { -1.0f, 0.0f, 0.4f }, { 0.3f, 0.8f, -0.6f }, { 0.6f, -0.6f, -0.6f },
      };
      const float d = drift_ * (1.0f / 6.0f);
      const float w1 = Clamp(kMix[i][0] * ax + kMix[i][1] * ay + kMix[i][2] * az, -1.0f, 1.0f);
      const float w2 = Clamp(kMix[(i + 3) % kNumSegments][0] * ax +
                             kMix[(i + 3) % kNumSegments][1] * ay +
                             kMix[(i + 3) % kNumSegments][2] * az, -1.0f, 1.0f);
      in.slider = Clamp(in.slider + d * w1, 0.0f, 1.0f);
      in.cv_slider += d * w1;
      in.pot = Clamp(in.pot + d * w2, 0.0f, 1.0f);
    }
    heard_slider_[i] = in.cv_slider;
    heard_pot_[i] = in.pot;
    float* out = output[i];
    if (type == TYPE_OSCILLATOR || type == TYPE_EFFECT) {
      if (type == TYPE_OSCILLATOR) {
        ProcessOscillator(i, config, in, out, size);
      } else {
        ProcessEffect(i, config, in, out, size);
      }
    } else {
      // A CV type: one step for the whole block (the gates folded into
      // one: rising / falling if any sample was, high as the block ends),
      // then a straight line from the last block's value to this one.
      stmlib::GateFlags g = in.gate[size - 1] & GATE_FLAG_HIGH;
      HOT_LOOP
      for (size_t n = 0; n < size; ++n) {
        g |= in.gate[n] & (GATE_FLAG_RISING | GATE_FLAG_FALLING);
      }
      in.gate = &g;
      g_step = static_cast<float>(size);
      g_step_i = static_cast<uint32_t>(size);
      g_jump = false;
      float o;
      switch (type) {
        case TYPE_RANDOM: ProcessRandom(i, config, in, &o, 1); break;
        case TYPE_CHAOS: ProcessChaos(i, config, in, &o, 1); break;
        case TYPE_ENVELOPE: ProcessEnvelope(i, config, in, &o, 1); break;
        default: ProcessProcessor(i, config, in, &o, 1); break;
      }
      g_step = 1.0f;
      g_step_i = 1;
      Segment& sg = segment_[i];
      const float norm = block_norm_[i][0];
      const float o0 = sg.cv_out, n0 = sg.cv_norm;
      const float inv = 1.0f / static_cast<float>(size);
      // (block_norm_ is only ever read at a block's first and last sample)
      if (g_jump) {
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) out[n] = o;
        block_norm_[i][0] = norm;
      } else {
        const float d = (o - o0) * inv;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          out[n] = o0 + d * static_cast<float>(n + 1);
        }
        block_norm_[i][0] = n0 + (norm - n0) * inv;
      }
      block_norm_[i][size - 1] = norm;
      sg.cv_out = o;
      sg.cv_norm = norm;
    }
    if (type == TYPE_PROCESSOR || type == TYPE_EFFECT) {
      // -8 to +8 V, whatever came in (the other types stay inside by
      // construction, and the DAC clamps too)
      HOT_LOOP
      for (size_t n = 0; n < size; ++n) out[n] = Clamp(out[n], -1.0f, 1.0f);
    }
    block_out_[i] = out;
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
  // slew: the pot, as a fraction of the time between new values
  const float slew_b = s.flavour == RANDOM_KIN ? 0.2f : b;
  const float tau = slew_b * slew_b * s.period;
  const float slew = tau < 0.5f ? 1.0f : 1.0f / (1.0f + tau);
  if (slew == 1.0f || s.flavour == RANDOM_QUANTISED ||
      (s.flavour == RANDOM_WEATHER && run % 5 == 2)) {
    g_jump = true;   // clean steps, notes, rain
  }

  const float wander = PerStep(0.0005f);
  for (size_t n = 0; n < size; ++n) {
    if (s.flavour == RANDOM_ORBIT) {
      if (run == 0) {
        // The leader moves a point along a path; the run reads X, Y, radius.
        s.phase2 += rate * (0.25f * StepTime());
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
        s.x += (s.target - s.x) * wander;
        s.y += (s.walk - s.y) * wander;
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
        run_z_[i][n] = x * x + y * y;  // radius squared (no sqrt needed)
        s.value = x * 0.5f + 0.5f;
      } else {
        const int leader = i - run;
        const float v = run % 3 == 1 ? run_y_[leader][n]
            : (run % 3 == 2 ? run_z_[leader][n] * 2.0f - 1.0f : -run_x_[leader][n]);
        s.value = Clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);
      }
      s.lp = s.value;
    } else if (s.flavour == RANDOM_WEATHER) {
      // A sky: pressure fronts come and go; low pressure brings gusts, and
      // rain when the air is wet. The slider is how fast the weather moves.
      // Side by side, weather segments read one sky: the first gives the
      // sky (pressure with its gusts), then the wind, the rain (triggers),
      // the humidity, the pressure alone.
      const int leader = i - run;
      bool rain_gate = false;
      if (run == 0) {
        if (Tick(&s, in, rate * 0.1f, n)) {
          s.event = 1;
          float t = s.target + (Gauss() - 0.5f) * 0.8f;   // the next front
          if (t > 1.0f) t = 2.0f - t;
          if (t < 0.0f) t = -t;
          s.target = Clamp(t, 0.0f, 1.0f);
          float w = s.walk + (Gauss() - 0.5f) * 0.5f;      // and the damp
          if (w > 1.0f) w = 2.0f - w;
          if (w < 0.0f) w = -w;
          s.walk = Clamp(w, 0.0f, 1.0f);
        }
        const float slow = rate * 0.02f * StepTime();
        s.x += (s.target - s.x) * slow;          // pressure
        s.y += (s.walk - s.y) * slow * 0.5f;     // humidity
        const float low = 1.0f - s.x;
        // gusts: more often, and stronger, the lower the pressure
        if (Random::GetFloat() < (0.2f + low) * rate * 0.3f * StepTime()) {
          s.z = (0.3f + 0.7f * Random::GetFloat()) * (0.4f + 0.6f * low);
        }
        s.lp += (s.z - s.lp) * (rate * 0.5f * StepTime() + 2e-4f * g_step);
        s.z -= s.z * rate * 0.05f * StepTime();
        const float wind = Clamp(0.25f * low + s.lp, 0.0f, 1.0f);
        // rain: dust whose density follows the damp, when the pressure is low
        if (s.gate_samples) {
          --s.gate_samples;
        } else if (Random::GetFloat() <
                   s.y * s.y * low * low * rate * 4.0f * StepTime()) {
          s.gate_samples = Steps(160);  // 5 ms
          s.f_slow = 0.4f + 0.6f * Random::GetFloat();
        }
        run_x_[i][n] = s.x;
        run_y_[i][n] = wind;
        run_z_[i][n] = s.gate_samples ? s.f_slow : 0.0f;
        s.value = Clamp(0.65f * s.x + 0.35f * wind, 0.0f, 1.0f);
      } else {
        switch (run % 5) {
          case 1: s.value = run_y_[leader][n]; break;
          case 2: s.value = run_z_[leader][n]; rain_gate = true; break;
          case 3: s.value = Clamp(segment_[leader].y, 0.0f, 1.0f); break;
          case 4: s.value = run_x_[leader][n]; break;
          default: s.value = run_y_[leader][n]; break;
        }
        if (rain_gate && s.value > 0.0f && s.lp == 0.0f) s.event = 1;
        s.lp = rain_gate ? s.value : 0.0f;
      }
      if (rain_gate) {  // triggers, never bipolar
        out[n] = s.value * aux;
        block_norm_[i][n] = s.value;
        continue;
      }
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
      if (slew == 1.0f) {
        s.lp = s.target;  // no slew: a clean step
      } else {
        s.lp += (s.target - s.lp) * slew;
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
  const float dust_chance = ExpMap(Clamp(in.cv_slider, 0.0f, 1.2f), 0.1f, 50.0f) *
      StepTime();
  const float follow = PerStep(0.02f);

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
          s.lp += (s.target - s.lp) * follow;
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
        s.lp += (s.target - s.lp) * follow;
        v = s.lp;
        break;

      case CHAOS_LORENZ:
        if (run == 0) {
          const float rho = 20.0f + b * 30.0f;
          // Euler steps of at most 0.002 (slider = speed): one a sample
          // at the fastest, as before
          const float h_all = rate * 2e-5f * g_step;
          int sub = 1 + static_cast<int>(h_all * 500.0f);
          const float h = h_all / static_cast<float>(sub);
          float x = s.x, y = s.y, z = s.z;
          while (sub--) {
            const float dx = 10.0f * (y - x);
            const float dy = x * (rho - z) - y;
            const float dz = x * y - (8.0f / 3.0f) * z;
            x += dx * h;
            y += dy * h;
            z += dz * h;
          }
          if (!(fabsf(x) < 200.0f && fabsf(y) < 200.0f && fabsf(z) < 200.0f)) {
            x = 0.1f; y = 0.0f; z = 0.0f;
          }
          s.x = x; s.y = y; s.z = z;
          run_x_[i][n] = Clamp((x + 25.0f) * 0.02f, 0.0f, 1.0f);
          run_y_[i][n] = Clamp((y + 30.0f) * (1.0f / 60.0f), 0.0f, 1.0f);
          run_z_[i][n] = Clamp(z * (1.0f / 60.0f), 0.0f, 1.0f);
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
          v = static_cast<float>(s.cells & 0xff) * kInv255;
        } else {
          v = static_cast<float>((run_cells_[leader][n] >> (8 * run)) & 0xff) * kInv255;
        }
        break;

      case CHAOS_COIN:
        // A coin toss per clock: heads opens the gate for half a period.
        if (Tick(&s, in, rate, n)) {
          if (Random::GetFloat() < b) {
            s.event = 1;
            s.gate_samples = static_cast<uint32_t>(s.period * 0.5f);
            if (s.gate_samples < Steps(150)) s.gate_samples = Steps(150);
          }
        }
        gate_out = true;
        v = s.gate_samples ? 1.0f : 0.0f;
        if (s.gate_samples) --s.gate_samples;
        break;

      case CHAOS_STAIR: {
          // A row of stair segments is one stepped sequence. Each slider (+
          // CV) is its step's height, each knob the chance its step plays (a
          // step that loses the roll is skipped). The first segment's gate
          // jack is the clock; with none, its knob is the speed and its step
          // always plays. The first segment outputs the stairs, the others a
          // gate while their step is on.
          if (run == 0) {
            int length = 1;
            while (i + length < kNumSegments &&
                   c.segment[i + length].type == TYPE_CHAOS &&
                   c.flavour(i + length) == CHAOS_STAIR) {
              ++length;
            }
            const float speed = ExpMap(b, 0.25f, 16.0f);
            if (Tick(&s, in, speed, n)) {
              s.event = 1;
              int next = s.step;
              for (int tries = 0; tries < length; ++tries) {
                next = (next + 1) % length;
                const float chance = (next == 0 && !in.patched)
                    ? 1.0f : Clamp(heard_pot_[i + next], 0.0f, 1.0f);
                if (Random::GetFloat() < chance * 1.02f) break;
                if (tries == length - 1) next = s.step;   // all lost: stay
              }
              s.step = static_cast<uint8_t>(next);
            }
            run_cells_[i][n] = s.step;
            v = Clamp(heard_slider_[i + s.step], 0.0f, 1.0f);
          } else {
            const bool on = run_cells_[leader][n] == static_cast<uint64_t>(run);
            if (on && s.value == 0.0f) s.event = 1;
            v = on ? 1.0f : 0.0f;
            gate_out = true;
          }
        }
        break;

      case CHAOS_DUST: {
          // Triggers at random moments, no clock. Slider = density.
          if (Random::GetFloat() < dust_chance) {
            s.event = 1;
            s.gate_samples = Steps(160);  // 5 ms
            s.target = 1.0f - b * Random::GetFloat();
          }
          gate_out = true;
          v = s.gate_samples ? s.target : 0.0f;
          if (s.gate_samples) --s.gate_samples;
        }
        break;
    }
    s.value = v;
    if (gate_out || s.flavour == CHAOS_CELLULAR || s.flavour == CHAOS_STAIR) {
      g_jump = true;
    }
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
  const float scale = Exp2(-in.cv * 8.0f);
  const float attack = ExpMap(Clamp(in.slider, 0.0f, 1.0f), 0.001f, 10.0f) * scale;
  const float decay = ExpMap(Clamp(in.pot, 0.0f, 1.0f), 0.001f, 10.0f) * scale;
  const float attack_inc = 1.0f / (attack * StepRate());
  const float decay_inc = 1.0f / (decay * StepRate());
  const float shape = CurveK(c.aux(i));
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
    int i, const Config& c, const Input& in, float* out, size_t) {
  // Always a full block (the module's blocks are 8 samples; stages.cc checks
  // it): a constant length lets the compiler unroll every loop below.
  const size_t size = kMaxBlockSize;
  Segment& s = segment_[i];
  // Slider: C1 to C7. CV: 1 V/oct. Hold + pot: fine tune, +/- 1 semitone.
  const float note = 24.0f + Clamp(in.slider, 0.0f, 1.0f) * 72.0f +
      in.cv * 96.0f + (c.aux(i) - 0.5f) * 2.0f;
  float f = 440.0f * Exp2((note - 69.0f) / 12.0f);
  f = Clamp(f, 0.5f, 12000.0f);
  // The pitch glides across the block, from the last block's to this one's:
  // no zipper steps when the slider, the CV or a sweep moves it.
  const float dt = f * kInvSampleRate;
  const uint32_t inc_to = Turns(dt);
  if (s.inc == 0) s.inc = inc_to;
  const uint32_t inc_from = s.inc;
  const float dt_from = static_cast<float>(inc_from) * kInvTurn;
  const float inv_size = 1.0f / static_cast<float>(size);
  const int32_t inc_step = static_cast<int32_t>(
      (static_cast<float>(inc_to) - static_cast<float>(inc_from)) * inv_size);
  s.inc = inc_to;
  const float b = Clamp(in.pot, 0.0f, 1.0f);
  const bool voice = c.alt(i);
  // The output level, folded into each flavour's loop; with the built-in
  // VCA (voice mode) it is applied after, sample by sample.
  const float scale = s.flavour == OSC_RIDER ? 1.0f : kAudioPeak;
  const float g = voice ? 1.0f : scale;

  // Pass 1: the phase of every sample. The gate is hard sync (not in voice
  // mode, where it opens the VCA).
  uint32_t phase[kMaxBlockSize];
  {
    uint32_t ph = s.ph;
    uint32_t inc = inc_from;
    if (in.patched && !voice) {
      HOT_LOOP
      for (size_t n = 0; n < size; ++n) {
        inc += inc_step;
        if (in.gate[n] & GATE_FLAG_RISING) ph = 0;
        phase[n] = ph;
        ph += inc;
      }
    } else {
      HOT_LOOP
      for (size_t n = 0; n < size; ++n) {
        inc += inc_step;
        phase[n] = ph;
        ph += inc;
      }
    }
    s.ph = ph;
  }

  // Pass 2: each flavour in its own loop, into out[].
  switch (s.flavour) {
    case OSC_MORPH:
    case OSC_BANK: {
        // band-limited tables for the higher of the two pitches
        const Mip mip = BankMip(dt > dt_from ? dt : dt_from);
        const bool two = mip.lo != mip.hi && mip.w < 0.996f;
        if (s.flavour == OSC_MORPH) {
          const BankWave tri = GetBankWave(1, mip);
          const BankWave saw = GetBankWave(2, mip);
          const float m = b * 3.999f;
          const int k = static_cast<int>(m);
          const float frac = m - static_cast<float>(k);
          if (two) {
            RenderMorph<true>(k, frac, tri, saw, kBankGain[1], kBankGain[2], g, phase, out, size);
          } else {
            RenderMorph<false>(k, frac, tri, saw, kBankGain[1], kBankGain[2], g, phase, out, size);
          }
        } else {
          const float m = b * (kBankTables - 1.001f);
          const int k = static_cast<int>(m);
          const float frac = m - static_cast<float>(k);
          const BankWave wa = GetBankWave(k, mip);
          const BankWave wb = GetBankWave(k + 1, mip);
          if (two) {
            RenderBank<true>(wa, wb, frac, g * kInv32000 * kTableUnit, phase, out, size);
          } else {
            RenderBank<false>(wa, wb, frac, g * kInv32000 * kTableUnit, phase, out, size);
          }
        }
      }
      break;

    case OSC_ADDITIVE: {
        // 1 to 16 harmonics at 1/k, the fading last one, none above 14 kHz.
        // Summed by Clenshaw's recurrence on 2 cos(p): one multiply-add and
        // one subtract per harmonic, highest first.
        const float harmonics = 1.0f + b * 15.0f;
        int top = static_cast<int>(14000.0f / f);
        if (top > 16) top = 16;
        if (top < 1) top = 1;
        int whole = static_cast<int>(harmonics);
        float last_weight = harmonics - static_cast<float>(whole);
        if (whole >= top) {
          whole = top;
          last_weight = 0.0f;
        }
        // (the weights carry the level: worked out at run time, they stay
        // in registers instead of being reloaded from flash every sample)
        const float level = 0.6f * g;
        float a[17];
        for (int k = 1; k <= 16; ++k) a[k] = kHarmonicWeight[k] * level;
        // the fading one starts the recurrence (b1 = its weight, b2 = 0);
        // the whole ones follow, jumping into a straight run of steps
        const float first = last_weight > 0.0f  // (whole < top <= 16 here)
            ? a[whole + 1] * last_weight : 0.0f;
// (a - b2 first: VFMA accumulates into its operand, so this order needs no
// copy of the weight)
#define ADDITIVE_STEP(k) { const float bk = __builtin_fmaf(cs2, b1, a[k] - b2); b2 = b1; b1 = bk; }
        for (size_t n = 0; n < size; ++n) {
          const uint32_t p = phase[n];
          const float cs2 = 2.0f * SinU(p + 0x40000000u);
          float b1 = first;
          float b2 = 0.0f;
          switch (whole) {   // falls through: whole, whole - 1, ... 1
            case 16: ADDITIVE_STEP(16)
            case 15: ADDITIVE_STEP(15)
            case 14: ADDITIVE_STEP(14)
            case 13: ADDITIVE_STEP(13)
            case 12: ADDITIVE_STEP(12)
            case 11: ADDITIVE_STEP(11)
            case 10: ADDITIVE_STEP(10)
            case 9: ADDITIVE_STEP(9)
            case 8: ADDITIVE_STEP(8)
            case 7: ADDITIVE_STEP(7)
            case 6: ADDITIVE_STEP(6)
            case 5: ADDITIVE_STEP(5)
            case 4: ADDITIVE_STEP(4)
            case 3: ADDITIVE_STEP(3)
            case 2: ADDITIVE_STEP(2)
            default: ADDITIVE_STEP(1)
          }
          out[n] = b1 * SinU(p);
        }
#undef ADDITIVE_STEP
      }
      break;

    case OSC_PAIR: {
        // Two sines; between them the ear hears a third, lower tone.
        const float ratio = 1.0f + b * 0.5f;
        const uint32_t inc2_to = Turns(dt * ratio);
        if (s.inc2 == 0) s.inc2 = inc2_to;
        uint32_t inc2 = s.inc2;
        const int32_t inc2_step = static_cast<int32_t>(
            (static_cast<float>(inc2_to) - static_cast<float>(inc2)) * inv_size);
        s.inc2 = inc2_to;
        uint32_t ph2 = s.ph2;
        const float h = 0.5f * g;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          inc2 += inc2_step;
          ph2 += inc2;
          out[n] = (SinU(phase[n]) + SinU(ph2)) * h;
        }
        s.ph2 = ph2;
      }
      break;

    case OSC_FOLD: {
        // Folded at twice the rate (the half-way sample, then this one),
        // then back down through a half-band filter. The half-way sine
        // comes from its neighbours:
        // sin(a) + sin(b) = 2 sin((a + b) / 2) cos((b - a) / 2).
        // The folder's depth: x1 to x7, eased off in the top octaves so its
        // harmonics stay mostly below 47 kHz (about 10 per unit of depth).
        float fold_gain = 1.0f + b * 6.0f;
        const float fold_limit = 0.15f / (dt > dt_from ? dt : dt_from);
        if (fold_gain > fold_limit) fold_gain = fold_limit > 1.0f ? fold_limit : 1.0f;
        const float fold_mid = fold_gain * 0.5f / Cos(0.5f * dt);
        // w: the 18 folded values before this block (oldest first), then
        // this block's 16 (the half-way one, then the sample's own)
        float w[18 + 2 * kMaxBlockSize];
        for (int k = 0; k < 18; ++k) w[k] = s.fold_z[k];
        float last = s.fold_sin;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float sine = SinU(phase[n]);
          w[18 + 2 * n] = Fold((last + sine) * fold_mid);
          w[19 + 2 * n] = Fold(sine * fold_gain);
          last = sine;
        }
        s.fold_sin = last;
        const float c0 = 0.5f * g;
        const float c1 = 0.307808409f * g;
        const float c2 = -0.077682721f * g;
        const float c3 = 0.025546120f * g;
        const float c4 = -0.006282386f * g;
        const float c5 = 0.000526214f * g;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float* z = w + 2 * n;  // z[19]: this sample, z[10]: the centre tap
          out[n] = c0 * z[10] + c1 * (z[9] + z[11]) + c2 * (z[7] + z[13]) +
              c3 * (z[5] + z[15]) + c4 * (z[3] + z[17]) + c5 * (z[1] + z[19]);
        }
        for (int k = 0; k < 18; ++k) s.fold_z[k] = w[2 * kMaxBlockSize + k];
      }
      break;

    case OSC_FM:
      // The left neighbour is the modulator (an oscillator for FM, a
      // random or envelope for slow phase bending); pot = depth.
      if (i > 0) {
        // the phase offset in turns times 2^28, then shifted up: the integer
        // wraps it. (Every output stays within +/-1, so |offset| < 3.3
        // turns, well inside the 8 this has room for.)
        const float depth = b * 2.0f * kInvAudioPeak * 268435456.0f;
        const float* m = block_out_[i - 1];
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float x = m[n] * depth;
          const uint32_t offset = static_cast<uint32_t>(static_cast<int32_t>(x)) << 4;
          out[n] = SinU(phase[n] + offset) * g;
        }
      } else {
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) out[n] = SinU(phase[n]) * g;
      }
      break;

    case OSC_GENDY: {
        // Xenakis' dynamic stochastic synthesis: 12 breakpoints whose
        // heights and spacing take a random walk; pot = how far. Side by
        // side, Gendy segments play the first one's breakpoints, each at
        // its own pitch: one waveform, moving as a cloud.
        const int gendy_run = RunPosition(i, c);
        Segment& gs = gendy_run > 0 ? segment_[i - gendy_run] : s;
        const float dt_step = (dt - dt_from) * inv_size;
        // (the per-sample state in locals: out[] could alias it)
        float phase2 = s.phase2;
        float dc = s.dc;
        float inv = s.gendy_inv;
        for (size_t n = 0; n < size; ++n) {
          const int k = s.gendy_index;
          const int k2 = (k + 1) % 12;
          const float dtn = dt_from + dt_step * static_cast<float>(n + 1);
          phase2 += dtn * gs.gendy_sum * inv;
          bool advanced = false;
          if (phase2 >= 1.0f) {
            phase2 -= 1.0f;
            s.gendy_index = k2;
            advanced = true;
            if (k2 == 0) s.event = 1;
          }
          if (phase2 >= 1.0f) phase2 = 0.0f;
          if (gendy_run == 0 && advanced) {
            const float step = b * 0.35f;
            float a = s.gendy_amp[k2] + (Gauss() - 0.5f) * 2.0f * step;
            if (a > 1.0f) a = 2.0f - a;
            if (a < -1.0f) a = -2.0f - a;
            s.gendy_amp[k2] = a;
            float d = s.gendy_dur[k2] + (Gauss() - 0.5f) * 2.0f * step;
            d = Clamp(d, 0.4f, 2.5f);
            s.gendy_sum += d - s.gendy_dur[k2];
            s.gendy_dur[k2] = d;
          }
          if (advanced) {
            inv = 1.0f / gs.gendy_dur[k2];  // the next segment's length
          }
          const int a = s.gendy_index;
          const int a2 = (a + 1) % 12;
          float y = gs.gendy_amp[a] + (gs.gendy_amp[a2] - gs.gendy_amp[a]) * phase2;
          y -= dc;
          dc += y * 0.0005f;
          out[n] = y * g;
        }
        s.phase2 = phase2;
        s.dc = dc;
        s.gendy_inv = inv;
      }
      break;

    case OSC_RIDER: {
        // Writes the left neighbour (segment 1: its own CV jack) into its
        // waveform, one full table every 0.05-10 s (pot); gate = freeze.
        // (Already in output units: 1.0 = 8 V.)
        const float rider_inc = kRiderLength * kInvSampleRate /
            ExpMap(b, 0.05f, 10.0f);
        int16_t* t = memory_->rider[i];
        float write = s.rider_write;
        float dc = s.dc;
        for (size_t n = 0; n < size; ++n) {
          const bool freeze = in.patched && (in.gate[n] & GATE_FLAG_HIGH);
          if (!freeze) {
            const float source = i > 0 ? block_out_[i - 1][n] : in.cv_raw;
            const int w = static_cast<int>(write);
            t[w] = static_cast<int16_t>(Clamp(source, -1.0f, 1.0f) * 32767.0f);
            write += rider_inc;
            if (write >= kRiderLength) {
              write -= kRiderLength;
              s.event = 1;
            }
          }
          const uint32_t p = phase[n];
          const uint32_t j = p >> 24;   // kRiderLength = 256
          const float fx = static_cast<float>(p << 8) * kInvTurn;
          const float t0 = t[j];
          const float v = (t0 + (t[(j + 1) & (kRiderLength - 1)] - t0) * fx) * kInv32767;
          dc += (v - dc) * 0.0002f;
          out[n] = Clamp(v - dc, -1.0f, 1.0f);
        }
        s.rider_write = write;
        s.dc = dc;
      }
      break;
  }

  // Pass 3, voice mode only: the VCA (2 ms attack, 300 ms release), once
  // per block like the CV types, gliding across it. The coefficients are
  // OnePoleCoefficient(0.002f) and (0.3f) at 31250 Hz, for 8 samples:
  // 1 - (1 - c)^8.
  if (voice) {
    const float kAttack = 0.12014662f;
    const float kRelease = 0.00085296935f;
    bool open = true;
    if (in.patched) {
      stmlib::GateFlags any = 0;
      HOT_LOOP
      for (size_t n = 0; n < size; ++n) any |= in.gate[n];
      if (any & GATE_FLAG_RISING) s.event = 1;
      open = (in.gate[size - 1] & GATE_FLAG_HIGH) || (any & GATE_FLAG_RISING);
    }
    const float v0 = s.voice;
    s.voice += ((open ? 1.0f : 0.0f) - v0) * (open ? kAttack : kRelease);
    const float dv = (s.voice - v0) * (scale / static_cast<float>(size));
    float v = v0 * scale;
    HOT_LOOP
    for (size_t n = 0; n < size; ++n) {
      v += dv;
      out[n] *= v;
    }
  } else {
    s.voice = 1.0f;
  }
  // For the neighbours: block_norm_ is read at the block's first sample
  // (a CV type's one step) and its last (cords). (It may overshoot 0-1 by
  // the few percent of a band-limited edge's ringing; its readers allow
  // for it.)
  block_norm_[i][0] = 0.5f + 0.5f * kInvAudioPeak * out[0];
  block_norm_[i][size - 1] = 0.5f + 0.5f * kInvAudioPeak * out[size - 1];
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
  const float dc = PerStep(0.001f);
  if (s.flavour == PROC_TRANSIENT || s.flavour == PROC_QUANTISE ||
      s.flavour == PROC_COMPARE || s.flavour == PROC_CRUSH ||
      (s.flavour == PROC_SAMPLE && b == 0.0f)) {
    g_jump = true;   // triggers, notes, gates, held steps
  }

  switch (s.flavour) {
    case PROC_FOLLOW: {
        // The BRIBESTAGES follower: slider = release, pot = attack,
        // hold + pot = gain x1-x8, long press = invert, gate = freeze.
        const float gain = (1.0f + aux * 7.0f) * kInvAudioPeak;
        const float release = OnePoleCoefficient(ExpMap(slider, 0.005f, 5.0f));
        const float attack = OnePoleCoefficient(b * 0.25f);
        for (size_t n = 0; n < size; ++n) {
          s.f_dc += (x - s.f_dc) * dc;
          const float rect = fabsf(x - s.f_dc) * gain;
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
        const float gain = (1.0f + aux * 7.0f) * kInvAudioPeak;
        const float threshold = 0.02f + slider * slider * 0.6f;
        const uint32_t length = static_cast<uint32_t>(
            ExpMap(b, 0.002f, 0.1f) * StepRate());
        const float fast_up = OnePoleCoefficient(0.0005f);
        const float fast_down = OnePoleCoefficient(0.02f);
        const float slow = OnePoleCoefficient(0.1f);
        for (size_t n = 0; n < size; ++n) {
          s.f_dc += (x - s.f_dc) * dc;
          const float rect = fabsf(x - s.f_dc) * gain;
          s.f_env += (rect - s.f_env) * (rect > s.f_env ? fast_up : fast_down);
          s.f_slow += (rect - s.f_slow) * slow;
          if (s.refractory) --s.refractory;
          // re-arm only once the hit has died down, so one hit = one trigger
          if (!s.armed && !s.refractory && s.f_env < s.f_slow * 1.2f + threshold * 0.5f) {
            s.armed = true;
          }
          if (s.armed && s.f_env > s.f_slow * 1.5f + threshold) {
            s.gate_samples = length;
            s.refractory = Steps(1500);  // 48 ms at least between triggers
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
            s.held = Fold((x - s.f_dc) * kInvAudioPeak * (1.0f + slider * 7.0f));
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

    case PROC_QUANTISE: {
        // The CV jack (+ the cord) onto a scale: Sleepcousonique's 38, picked
        // with the knob. Slider: transpose, -2 to +2 octaves in whole
        // octaves. Hold + knob: the root, 0-11 semitones up. Gate patched:
        // a new note only on its rising edge. Long press: a trigger out on
        // every new note instead of the pitch.
        const int scale_index = static_cast<int>(Clamp(b * kNumScales, 0.0f, kNumScales - 1.0f));
        // (found again only when the knob picks another: the walk through
        // the packed list is up to 38 steps)
        if (!s.q_scale || s.q_scale_index != scale_index) {
          s.q_scale = FindScale(scale_index);
          s.q_scale_index = scale_index;
        }
        const float* scale = s.q_scale;
        const float root = Floor(aux * 11.99f) * 100.0f;
        const float octaves = Floor(slider * 4.99f) - 2.0f;
        const float cents_in = in.cv * 9600.0f - root;
        bool sample = !in.patched;
        for (size_t n = 0; n < size; ++n) {
          if (in.patched && (in.gate[n] & GATE_FLAG_RISING)) sample = true;
        }
        if (sample) {
          // a little hysteresis: stay on the note unless the input is
          // clearly nearer another one
          const float cand = NearestDegree(scale, cents_in);
          if (cand != s.q_cents &&
              fabsf(cents_in - cand) + 8.0f < fabsf(cents_in - s.q_cents)) {
            s.q_cents = cand;
            s.event = 1;
            s.gate_samples = Steps(160);
          }
        }
        const float v = (s.q_cents + root) * (1.0f / 9600.0f) + octaves * 0.125f;
        for (size_t n = 0; n < size; ++n) {
          if (alt) {
            out[n] = s.gate_samples ? kAudioPeak : 0.0f;
            if (s.gate_samples) --s.gate_samples;
          } else {
            out[n] = v;
          }
          block_norm_[i][n] = Clamp(v, 0.0f, 1.0f);
        }
        s.norm = Clamp(v * 1.6f, 0.0f, 1.0f);
      }
      break;

    case PROC_SAMPLE: {
        // Sample and hold the CV jack on each rising gate (with no gate: an
        // internal clock, slider = rate 0.05-100 Hz). Knob: glide to the new
        // value (0-1 s). Long press: track and hold (follows while the gate
        // is high, holds while low).
        const float rate = ExpMap(slider, 0.05f, 100.0f);
        const float glide = OnePoleCoefficient(b * b);
        for (size_t n = 0; n < size; ++n) {
          bool take;
          if (alt && in.patched) {
            take = (in.gate[n] & GATE_FLAG_HIGH) != 0;
          } else {
            take = Tick(&s, in, rate, n);
          }
          if (take) {
            if (!alt) s.event = 1;
            s.held = in.cv;
          }
          s.lp += (s.held - s.lp) * glide;
          out[n] = s.lp;
          block_norm_[i][n] = Clamp(fabsf(s.lp), 0.0f, 1.0f);
        }
        s.norm = Clamp(fabsf(s.lp), 0.0f, 1.0f);
      }
      break;

    case PROC_COMPARE: {
        // A gate (5 V) while the CV jack is above the slider's level (-5 to
        // +8 V); the knob is the hysteresis (0-1 V). Long press: a window, the
        // gate is open while the input is within the knob's distance of the
        // slider. Hold + knob: the gate's height, 5 V up to 8 V.
        const float level = -0.625f + slider * 1.625f;
        const float width = b * 0.125f;
        const float gain = (1.0f + aux * 0.6f) * kAudioPeak;
        for (size_t n = 0; n < size; ++n) {
          const float x2 = in.cv;
          bool open;
          if (alt) {
            open = fabsf(x2 - level) < width + 0.005f;
          } else {
            const float edge = s.held > 0.5f ? level - width * 0.5f : level + width * 0.5f;
            open = x2 > edge;
          }
          if (open && s.held < 0.5f) s.event = 1;
          s.held = open ? 1.0f : 0.0f;
          out[n] = s.held * gain;
          block_norm_[i][n] = s.held;
        }
        s.norm = s.held;
      }
      break;

    default: {  // PROC_LOOPER
        // Gate high records the CV jack (64 times a second, up to 12 s);
        // gate low loops it. Slider = speed 1/4x-4x (noon 1x), pot = smoothing,
        // long press = backwards.
        const float speed = Exp2((slider - 0.5f) * 4.0f);
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
            s.rec_clock += 64.0f * StepTime();
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
            s.play += (alt ? -speed : speed) * (64.0f * StepTime());
            while (s.play >= len) s.play -= len;
            while (s.play < 0.0f) s.play += len;
            const int j = static_cast<int>(s.play);
            const float fx = s.play - j;
            const int j2 = (j + 1) % s.loop_length;
            v = (buffer[j] + (buffer[j2] - buffer[j]) * fx) * kInv32767;
            if (j == 0 && fx < (64.0f * StepTime()) * speed) s.event = 1;
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

// ---------------------------------------------------------------------------
// EFFECT
//
// The left neighbour's output, sample by sample at the full rate (segment 1
// takes its own CV jack instead, read once a block: lo-fi). Slider + CV
// (and the cord) is the main control, the knob the second, hold + knob the
// third, the long press an option. Comb, chorus and echo use this segment's
// looper memory (768 samples).

namespace {

inline float NoteToF(float note) {   // in cycles per sample
  return 440.0f * Exp2((note - 69.0f) * (1.0f / 12.0f)) * kInvSampleRate;
}

inline float TanPi(float f) {        // tan(pi f), f < 0.45 (stmlib's FAST)
  const float a = 3.260e-01f * 31.00627668f;
  const float b = 1.823e-01f * 306.0196848f;
  const float f2 = f * f;
  return f * (3.14159265f + f2 * (a + b * f2));
}

// pos in [0, 2 * kLooperLength): every reader is a write position plus the
// line's length minus a delay shorter than the line.
inline float ReadLine(const int16_t* line, float pos) {
  int j = static_cast<int>(pos);
  const float fx = pos - static_cast<float>(j);
  if (j >= kLooperLength) j -= kLooperLength;
  const int j1 = j + 1 < kLooperLength ? j + 1 : 0;
  const int16_t a = line[j];
  const int16_t b = line[j1];
  return (a + (b - a) * fx) * kInv32767;
}

// -1 to +1 into the line's 16 bits, saturating (integer compares: cheaper
// than a float clamp on this FPU). |v| < 65000 here.
inline int16_t ToLine(float v) {
  const int32_t q = static_cast<int32_t>(v * 32767.0f);
  return static_cast<int16_t>(q > 32767 ? 32767 : (q < -32767 ? -32767 : q));
}

}  // namespace

void Engine::ProcessEffect(
    int i, const Config& c, const Input& in, float* out, size_t) {
  // Always a full block (see ProcessOscillator).
  const size_t size = kMaxBlockSize;
  Segment& s = segment_[i];
  const float slider = Clamp(in.slider, 0.0f, 1.0f);
  const float b = Clamp(in.pot, 0.0f, 1.0f);
  const float aux = c.aux(i);
  const bool alt = c.alt(i);
  int16_t* line = memory_->looper[i];
  const float* source = i > 0 ? block_out_[i - 1] : NULL;
  const float pitch_note = 24.0f + slider * 72.0f + in.cv * 96.0f;

  // per-block settings
  float g = 0.0f, r = 0.0f, h = 0.0f, level = 0.0f, lpg = 0.0f;
  float decay_coef = 0.0f, attack_coef = 0.0f, dt = 0.0f, fb = 0.0f;
  float delay = 0.0f, depth = 0.0f, mix = 0.0f, damp = 0.0f, drive = 0.0f;
  int hold = 1;
  float quantum = 0.0f;
  switch (s.flavour) {
    case FX_FILTER: {
        // a state-variable filter. Slider + CV: cutoff, 20 Hz to 13 kHz,
        // 1 V/oct. Knob: resonance (up to self-oscillation). Hold + knob:
        // low-pass -> band-pass -> high-pass. Long press: drive into it.
        float f = NoteToF(16.0f + slider * 112.0f + in.cv * 96.0f);
        f = Clamp(f, 0.0003f, 0.42f);
        g = TanPi(f);
        r = 2.0f - 1.98f * b;      // 1/Q: 0.5 to 50
        h = 1.0f / (1.0f + r * g + g * g);
      }
      break;
    case FX_VCA: {
        // Slider + CV: level (linear; hold + knob curves it towards
        // exponential). Gate patched: it also opens on a gate (2 ms attack,
        // knob = decay 5 ms to 5 s). Long press: a low-pass gate, the sound
        // darkens as it closes.
        float l = Clamp(in.cv_slider, 0.0f, 1.2f);
        const float curve = 1.0f + aux * 3.0f;
        level = Exp2(curve * Log2(l + 1e-6f));
        attack_coef = OnePoleCoefficient(0.002f);
        decay_coef = OnePoleCoefficient(ExpMap(b, 0.005f, 5.0f));
        lpg = alt ? 1.0f : 0.0f;
      }
      break;
    case FX_RING: {
        // The sound times a carrier. Slider + CV: the carrier's pitch (C1 to
        // C7, 1 V/oct). Knob: dry -> ring. Hold + knob: carrier sine -> square.
        // Long press: the carrier is the CV jack (another sound, lo-fi).
        dt = Clamp(NoteToF(pitch_note), 0.0f, 0.45f);
        mix = b;
      }
      break;
    case FX_COMB: {
        // A tuned comb: the sound rings at the slider's pitch (C1 to C7, 1
        // V/oct, 41 Hz lowest). Knob: feedback (ring time). Hold + knob:
        // damping. Long press: negative feedback (hollow, an octave down).
        const float f = Clamp(NoteToF(pitch_note), 41.0f * kInvSampleRate, 0.2f);
        delay = Clamp(1.0f / f, 2.0f, kLooperLength - 2.0f);
        fb = b * 0.995f * (alt ? -1.0f : 1.0f);
        damp = 1.0f - 0.85f * aux;
      }
      break;
    case FX_CHORUS: {
        // A modulated delay. Slider + CV: speed, 0.05 to 8 Hz. Knob: depth.
        // Hold + knob: feedback (a flanger towards the right). Long press:
        // vibrato (only the moving copy).
        dt = ExpMap(Clamp(in.cv_slider, 0.0f, 1.2f), 0.05f, 8.0f) * kInvSampleRate;
        depth = b * 220.0f;
        fb = aux * 0.85f;
      }
      break;
    case FX_ECHO: {
        // A short, warm echo (stored 3906 times a second, up to 0.19 s).
        // Slider + CV: time. Knob: feedback. Hold + knob: dry -> wet. Long
        // press: hold, the echo repeats what it has for ever.
        delay = 4.0f + Clamp(in.cv_slider, 0.0f, 1.0f) * (kLooperLength - 8.0f);
        fb = alt ? 1.0f : b * 0.95f;
        mix = aux;
      }
      break;
    case FX_DRIVE: {
        // Slider + CV: drive, x1 to x16. Knob: soft clip -> wave folder.
        // Hold + knob: output level. Long press: asymmetric (even harmonics).
        drive = 1.0f + Clamp(in.cv_slider, 0.0f, 1.2f) * 15.0f;
        mix = b;
        level = 0.25f + aux * 1.0f;
      }
      break;
    default: {  // FX_LOFI
        // Slider + CV: sample rate, 31 kHz down to 100 Hz. Knob: bits, 16
        // down to 2. Hold + knob: output level. Long press: the rate jitters.
        const float rate = ExpMap(1.0f - Clamp(in.cv_slider, 0.0f, 1.0f), 100.0f, kSampleRate);
        hold = static_cast<int>(kSampleRate / rate);
        if (hold < 1) hold = 1;
        quantum = Exp2(-(15.0f - b * 14.0f));
        level = 0.25f + aux * 1.0f;
      }
      break;
  }

  // The sound in: the left neighbour, or (segment 1) its own CV jack.
  float jack[kMaxBlockSize];
  if (!source) {
    HOT_LOOP
    for (size_t n = 0; n < size; ++n) jack[n] = in.cv_raw;
    source = jack;
  }
  const int kL = kLooperLength;

  // Each flavour in its own loop, its state in locals (out[] could alias
  // the segment's floats, which would be reloaded every sample).
  switch (s.flavour) {
    case FX_FILTER: {
        // low -> band -> high: y = wl lp + wb bp + wh hp
        const float m = aux * 2.0f;
        const float wl = m < 1.0f ? 1.0f - m : 0.0f;
        const float wb = m < 1.0f ? r * m : r * (2.0f - m);
        const float wh = m < 1.0f ? 0.0f : m - 1.0f;
        const float rg = r + g;
        float f1 = s.f1, f2 = s.f2;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float x = source[n];
          const float xin = alt ? SoftClip(x * 3.0f * kInvAudioPeak) * kAudioPeak : x;
          const float hp = (xin - rg * f1 - f2) * h;
          const float bp = g * hp + f1;
          f1 = g * hp + bp;
          const float lp = g * bp + f2;
          f2 = g * bp + lp;
          out[n] = wl * lp + wb * bp + wh * hp;
        }
        // (once a block: blowing up takes far longer than 8 samples)
        if (!(fabsf(f1) < 50.0f && fabsf(f2) < 50.0f)) f1 = f2 = 0.0f;
        s.f1 = f1;
        s.f2 = f2;
      }
      break;

    case FX_VCA: {
        float env = s.fx_env, f1 = s.f1;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float x = source[n];
          float e = 1.0f;
          if (in.patched) {
            const stmlib::GateFlags gt = in.gate[n];
            const bool high = (gt & GATE_FLAG_HIGH) != 0;
            if (gt & GATE_FLAG_RISING) s.event = 1;
            env += ((high ? 1.0f : 0.0f) - env) * (high ? attack_coef : decay_coef);
            e = env;
          }
          const float gain = level * e;
          if (lpg > 0.0f) {
            const float coef = 0.005f + 0.6f * gain * gain;
            f1 += (x - f1) * (coef > 1.0f ? 1.0f : coef);
            out[n] = f1 * gain;
          } else {
            out[n] = x * gain;
          }
        }
        s.fx_env = env;
        s.f1 = f1;
      }
      break;

    case FX_RING: {
        float ph = s.fx_phase;
        const float jack_carrier = Clamp(in.cv_raw * kInvAudioPeak, -1.0f, 1.0f);
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float x = source[n];
          ph += dt;
          if (ph >= 1.0f) ph -= 1.0f;
          float carrier;
          if (alt) {
            carrier = jack_carrier;
          } else {
            const float sn = Sin(ph);
            carrier = sn + (SoftClip(sn * 8.0f) - sn) * aux;
          }
          out[n] = x + (x * carrier - x) * mix;
        }
        s.fx_phase = ph;
      }
      break;

    case FX_COMB: {
        int w = s.fx_write;
        float f1 = s.f1, f2 = s.f2;
        const float back = static_cast<float>(kL) - delay;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float x = source[n];
          if (++w >= kL) w = 0;
          const float d = ReadLine(line, static_cast<float>(w) + back);
          f1 += (d - f1) * damp;
          f2 += (x - f2) * 0.002f;   // no DC in (at full feedback it would pile up 200x)
          const float v = SoftClip(x - f2 + fb * f1);
          line[w] = ToLine(v);
          out[n] = v * 0.5f;
        }
        s.fx_write = w;
        s.f1 = f1;
        s.f2 = f2;
      }
      break;

    case FX_CHORUS: {
        float ph = s.fx_phase;
        int w = s.fx_write;
        const float centre = static_cast<float>(kL) -
            (30.0f + 250.0f * (fb > 0.3f ? 0.3f : 1.0f) + depth * 0.5f);
        const float swing = depth * 0.5f;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float x = source[n];
          ph += dt;
          if (ph >= 1.0f) ph -= 1.0f;
          if (++w >= kL) w = 0;
          const float wet = ReadLine(line, static_cast<float>(w) + centre - swing * Sin(ph));
          line[w] = ToLine(x + wet * fb);
          out[n] = alt ? wet : 0.5f * (x + wet);
        }
        s.fx_phase = ph;
        s.fx_write = w;
      }
      break;

    case FX_ECHO: {
        // stored once every 8 samples (the mean of the 8), read back with
        // a straight line between the stored values
        float held = s.fx_held;
        int count = s.fx_count;
        int w = s.fx_write;
        float f1 = s.f1, f2 = s.f2;
        const float back_pos = static_cast<float>(kL) - delay;
        for (size_t n = 0; n < size; ++n) {
          const float x = source[n];
          held += x;
          if (++count >= static_cast<int>(kMaxBlockSize)) {
            count = 0;
            if (++w >= kL) w = 0;
            const float back = ReadLine(line, static_cast<float>(w) + back_pos);
            f2 = f1;
            f1 = back;
            const float in8 = alt ? 0.0f : held * (1.0f / kMaxBlockSize);
            line[w] = ToLine(in8 + back * fb);
            held = 0.0f;
          }
          const float wet = f2 + (f1 - f2) * (static_cast<float>(count) * (1.0f / kMaxBlockSize));
          out[n] = x + (wet - x) * mix;
        }
        s.fx_held = held;
        s.fx_count = count;
        s.fx_write = w;
        s.f1 = f1;
        s.f2 = f2;
      }
      break;

    case FX_DRIVE: {
        float f1 = s.f1;
        const float k = kInvAudioPeak * drive;
        const float bias = alt ? 0.3f : 0.0f;
        const float out_level = kAudioPeak * level;
        HOT_LOOP
        for (size_t n = 0; n < size; ++n) {
          const float u = source[n] * k + bias;
          const float soft = SoftClip(u);
          const float folded = Fold(u * 0.5f);
          const float v = soft + (folded - soft) * mix;
          f1 += (v - f1) * 0.0005f;   // the asymmetry's DC
          out[n] = (v - f1) * out_level;
        }
        s.f1 = f1;
      }
      break;

    default: {  // FX_LOFI
        int count = s.fx_count;
        float held = s.fx_held;
        const float q = quantum * kAudioPeak;
        const float inv_q = 1.0f / q;
        for (size_t n = 0; n < size; ++n) {
          int h2 = hold;
          if (alt) h2 += static_cast<int>(Random::GetFloat() * hold);
          if (++count >= h2) {
            count = 0;
            held = Floor(source[n] * inv_q + 0.5f) * q;
          }
          out[n] = held * level;
        }
        s.fx_count = count;
        s.fx_held = held;
      }
      break;
  }

  // (Process clamps the output to +/-1.) For the neighbours: the first and
  // the last sample; for the LED: the block's peak.
  float peak = 0.0f;
  HOT_LOOP
  for (size_t n = 0; n < size; ++n) {
    const float a = fabsf(out[n]);
    if (a > peak) peak = a;
  }
  if (peak > 1.0f) peak = 1.0f;
  block_norm_[i][0] = 0.5f + 0.5f * Clamp(out[0] * kInvAudioPeak, -1.0f, 1.0f);
  block_norm_[i][size - 1] = 0.5f + 0.5f * Clamp(out[size - 1] * kInvAudioPeak, -1.0f, 1.0f);
  s.f_env += (peak * kInvAudioPeak - s.f_env) * 0.05f;
  s.norm = Clamp(s.f_env, 0.0f, 1.0f);
}

}  // namespace dobbel
}  // namespace stages
