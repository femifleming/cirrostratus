// Monsoon HiFi firmware - shared DSP building blocks.
//
// Everything in monsoon/dsp is portable C++ (no STM32 headers) so that the
// exact same code runs in the firmware and in the host test harness.
//
// MIT License. Builds on ideas from Mutable Instruments' Clouds
// (c) Emilie Gillet, and Tom Burns' Monsoon port.

#ifndef MONSOON_DSP_CORE_H_
#define MONSOON_DSP_CORE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace monsoon {

const float kSampleRate = 48000.0f;
const size_t kBlockSize = 32;          // codec block, also scale-block size
const float kPi = 3.14159265358979323846f;
const float kTwoPi = 2.0f * kPi;

struct FloatFrame {
  float l;
  float r;
};

struct ShortFrame {
  int16_t l;
  int16_t r;
};

// What a mode sees each block. Knobs are 0..1, pitch is in semitones.
struct Controls {
  float position;
  float size;
  float density;
  float texture;
  float pitch;       // knob + V/oct, -48..+48 semitones
  float blend;       // WET knob + CV (the engine's dry/wet blend)
  // The three small knobs below WET are per-mode controls (each mode
  // documents what they do). Conventions: BAL is bipolar with the v1.0
  // sound at noon; FBK and RVB give the v1.0 sound at zero.
  float bal;         // BAL knob + CV, 0..1
  float fbk;         // FBK knob + CV, 0..1
  float rvb;         // RVB knob + CV, 0..1
  bool freeze;       // current freeze state (button toggle or gate)
  bool trigger;      // rising edge on TRIG input during this block
  bool gate;         // TRIG input level
  uint8_t option;    // per-mode option chosen in the mode menu
};

// ---------------------------------------------------------------------------
// Memory: a bump allocator over two regions (main SRAM and CCM on the chip).
// Nothing in the DSP calls malloc.

class Arena {
 public:
  void Init(void* a, size_t a_size, void* b, size_t b_size) {
    base_[0] = static_cast<uint8_t*>(a); size_[0] = a_size;
    base_[1] = static_cast<uint8_t*>(b); size_[1] = b_size;
    Reset();
  }
  void Reset() { used_[0] = used_[1] = 0; }

  // Allocate from whichever region has room (largest-free first keeps big
  // buffers possible). Returns NULL when nothing fits.
  void* Allocate(size_t bytes) {
    bytes = (bytes + 7) & ~size_t(7);
    int first = free(0) >= free(1) ? 0 : 1;
    for (int k = 0; k < 2; ++k) {
      int i = k == 0 ? first : 1 - first;
      if (free(i) >= bytes) {
        void* p = base_[i] + used_[i];
        used_[i] += bytes;
        memset(p, 0, bytes);
        return p;
      }
    }
    return NULL;
  }
  template<typename T> T* Allocate(size_t n) {
    return static_cast<T*>(Allocate(n * sizeof(T)));
  }
  size_t free(int i) const { return size_[i] - used_[i]; }
  size_t largest_free() const { return free(0) > free(1) ? free(0) : free(1); }
  size_t used() const { return used_[0] + used_[1]; }
  size_t capacity() const { return size_[0] + size_[1]; }

 private:
  uint8_t* base_[2];
  size_t size_[2];
  size_t used_[2];
};

// ---------------------------------------------------------------------------
// Small math helpers.

inline float Clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

inline float Crossfade(float a, float b, float t) { return a + (b - a) * t; }

// A bipolar knob (BAL): 0..1 -> -1..+1, with a small dead zone at noon so a
// knob left roughly centred gives exactly the neutral setting.
inline float KnobBipolar(float k) {
  const float kDead = 0.03f;
  float x = k * 2.0f - 1.0f;
  if (x > kDead) return x > 1.0f ? 1.0f : (x - kDead) * (1.0f / (1.0f - kDead));
  if (x < -kDead) return x < -1.0f ? -1.0f : (x + kDead) * (1.0f / (1.0f - kDead));
  return 0.0f;
}

inline float SemitonesToRatio(float s) {
  return exp2f(s * (1.0f / 12.0f));
}

// Smooth odd saturator, unity slope at 0, approaches +/-1.
inline float SoftClip(float x) {
  if (x < -3.0f) return -1.0f;
  if (x > 3.0f) return 1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// 4-point, 3rd-order Hermite interpolation.
inline float Hermite(float xm1, float x0, float x1, float x2, float t) {
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float b_neg = w + a;
  return ((((a * t) - b_neg) * t + c) * t + x0);
}

// floor() for |x| < 2^31 with a single float->int conversion.
inline float FastFloor(float x) {
  float f = static_cast<float>(static_cast<int32_t>(x));
  return f > x ? f - 1.0f : f;
}

// Fast sine/cosine from a 1024-point table (phase in cycles, any value).
extern const float kSineTable[1025];
inline float FastSin(float phase) {
  phase -= FastFloor(phase);
  float idx = phase * 1024.0f;
  int i = static_cast<int>(idx);
  float f = idx - i;
  // A tiny negative phase can round to exactly 1.0 above: wrap it.
  i &= 1023;
  return kSineTable[i] + (kSineTable[i + 1] - kSineTable[i]) * f;
}
inline float FastCos(float phase) { return FastSin(phase + 0.25f); }

// atan2 approximation, max error ~0.0015 rad. Returns cycles in [-0.5, 0.5).
inline float FastAtan2Cycles(float y, float x) {
  const float ax = fabsf(x), ay = fabsf(y);
  const float mn = ax < ay ? ax : ay;
  const float mx = ax < ay ? ay : ax;
  const float a = mn / (mx + 1e-20f);
  const float s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0.0f) r = 3.14159274f - r;
  if (y < 0.0f) r = -r;
  return r * (1.0f / kTwoPi);
}

inline float FastSqrt(float x) { return sqrtf(x); }  // VSQRT on the M4

// xorshift32 random generator.
class Random {
 public:
  void Seed(uint32_t s) { state_ = s ? s : 0x1234567u; }
  inline uint32_t Next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }
  inline float Float() { return (Next() >> 8) * (1.0f / 16777216.0f); }
  inline float Bipolar() { return Float() * 2.0f - 1.0f; }
 private:
  uint32_t state_ = 0x1234567u;
};

// ---------------------------------------------------------------------------
// Filters.

class OnePole {
 public:
  void Init() { state_ = 0.0f; coef_ = 1.0f; }
  // Cutoff as a fraction of the sample rate.
  void set_f(float f) {
    f = Clamp(f, 0.0f, 0.497f);
    coef_ = 1.0f - expf(-kTwoPi * f);
  }
  void set_coefficient(float c) { coef_ = c; }
  inline float Lp(float x) { state_ += coef_ * (x - state_); return state_; }
  inline float Hp(float x) { return x - Lp(x); }
  float state() const { return state_; }
  void Reset(float v = 0.0f) { state_ = v; }
 private:
  float state_;
  float coef_;
};

// Zavalishin topology-preserving state variable filter.
class Svf {
 public:
  void Init() { ic1_ = ic2_ = 0.0f; set(0.01f, 0.5f); }
  // f is cutoff / sample rate, q resonance (0.5 = no peak).
  void set(float f, float q) {
    f = Clamp(f, 1e-5f, 0.49f);
    g_ = tanf(kPi * f);
    k_ = 1.0f / q;
    a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
  }
  inline void Process(float x, float* lp, float* bp, float* hp) {
    float v3 = x - ic2_;
    float v1 = a1_ * ic1_ + g_ * a1_ * v3;
    float v2 = ic2_ + g_ * v1;
    ic1_ = 2.0f * v1 - ic1_;
    ic2_ = 2.0f * v2 - ic2_;
    *lp = v2;
    *bp = v1;
    *hp = x - k_ * v1 - v2;
  }
  inline float Lp(float x) { float l, b, h; Process(x, &l, &b, &h); return l; }
  inline float Bp(float x) { float l, b, h; Process(x, &l, &b, &h); return b; }
  inline float BpNormalized(float x) {
    float l, b, h; Process(x, &l, &b, &h); return b * k_;
  }
  inline float Hp(float x) { float l, b, h; Process(x, &l, &b, &h); return h; }
 private:
  float g_, k_, a1_;
  float ic1_, ic2_;
};

class DcBlocker {
 public:
  void Init(float pole = 0.9995f) { x1_ = y1_ = 0.0f; pole_ = pole; }
  inline float Process(float x) {
    float y = x - x1_ + pole_ * y1_;
    x1_ = x;
    y1_ = y;
    return y;
  }
 private:
  float x1_, y1_, pole_;
};

// Linear smoothing of a control value across one block.
class BlockRamp {
 public:
  void Init(float v) { value_ = v; }
  // Returns per-sample increment; call Next() n times.
  void Start(float target, size_t n) {
    target_ = target;
    inc_ = (target - value_) / static_cast<float>(n);
  }
  inline float Next() { value_ += inc_; return value_; }
  float value() const { return value_; }
  void Finish() { value_ = target_; }
 private:
  float value_ = 0.0f, target_ = 0.0f, inc_ = 0.0f;
};

// Peak/RMS-ish envelope follower.
class Envelope {
 public:
  void Init(float attack_s, float release_s) {
    a_ = 1.0f - expf(-1.0f / (attack_s * kSampleRate));
    r_ = 1.0f - expf(-1.0f / (release_s * kSampleRate));
    v_ = 0.0f;
  }
  inline float Process(float x) {
    x = fabsf(x);
    v_ += (x > v_ ? a_ : r_) * (x - v_);
    return v_;
  }
  float value() const { return v_; }
 private:
  float a_, r_, v_;
};

// Allpass used for diffusion / decorrelation (float delay up to N).
template<size_t N>
class Allpass {
 public:
  void Init(size_t delay, float g) {
    delay_ = delay < N ? delay : N - 1;
    g_ = g;
    memset(line_, 0, sizeof(line_));
    pos_ = 0;
  }
  inline float Process(float x) {
    size_t rd = pos_ + N - delay_;
    if (rd >= N) rd -= N;
    float d = line_[rd];
    float v = x + g_ * d;
    line_[pos_] = v;
    if (++pos_ >= N) pos_ = 0;
    return d - g_ * v;
  }
 private:
  float line_[N];
  size_t delay_;
  size_t pos_;
  float g_;
};

}  // namespace monsoon

#endif  // MONSOON_DSP_CORE_H_
