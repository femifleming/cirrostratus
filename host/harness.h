// Host-side simulation of the module: same engine, same memory budget.

#ifndef MONSOON_HOST_HARNESS_H_
#define MONSOON_HOST_HARNESS_H_

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "monsoon/dsp/engine.h"
#include "monsoon/dsp/memory.h"

namespace host {

using monsoon::Controls;
using monsoon::FloatFrame;

inline Controls DefaultControls() {
  Controls c;
  c.position = 0.5f;
  c.size = 0.5f;
  c.density = 0.5f;
  c.texture = 0.5f;
  c.pitch = 0.0f;
  c.blend = 1.0f;     // fully wet so tests hear the mode
  c.spread = 0.5f;
  c.feedback = 0.0f;
  c.reverb = 0.0f;
  c.freeze = false;
  c.trigger = false;
  c.gate = false;
  c.option = 0;
  return c;
}

// The module: engine + its RAM blocks (heap-allocated with exact sizes so
// AddressSanitizer catches any overrun).
class Module {
 public:
  Module() : main_(monsoon::kMainMemoryBytes), ccm_(monsoon::kCcmMemoryBytes) {
    ok_ = engine_.Init(main_.data(), main_.size(), ccm_.data(), ccm_.size());
  }
  bool ok() const { return ok_; }
  monsoon::Engine& engine() { return engine_; }

  // Switches mode and runs until the switch has completed.
  bool SetMode(monsoon::ModeId mode, uint8_t option = 0) {
    engine_.RequestMode(mode, option);
    std::vector<FloatFrame> silence(monsoon::kBlockSize * 64);
    for (int tries = 0; tries < 200; ++tries) {
      Run(DefaultControls(), silence.data(), NULL, monsoon::kBlockSize * 4);
      if (!engine_.switching()) break;
    }
    return !engine_.init_failed();
  }

  // Processes `n` frames in codec-sized blocks, calling Prepare() after
  // every block (on the module the main loop runs between interrupts).
  void Run(const Controls& c, const FloatFrame* in, FloatFrame* out, size_t n,
           int prepare_calls_per_block = 4) {
    FloatFrame dummy[monsoon::kBlockSize];
    for (size_t i = 0; i + monsoon::kBlockSize <= n; i += monsoon::kBlockSize) {
      engine_.ProcessFloat(c, in + i, out ? out + i : dummy, monsoon::kBlockSize);
      for (int k = 0; k < prepare_calls_per_block; ++k) engine_.Prepare();
    }
  }

  // Runs with a per-block control callback (block index -> controls).
  std::vector<FloatFrame> Render(const std::vector<FloatFrame>& in,
                                 std::function<Controls(size_t)> controls) {
    std::vector<FloatFrame> out(in.size());
    for (size_t i = 0, b = 0; i + monsoon::kBlockSize <= in.size();
         i += monsoon::kBlockSize, ++b) {
      Controls c = controls(b);
      engine_.ProcessFloat(c, &in[i], &out[i], monsoon::kBlockSize);
      for (int k = 0; k < 4; ++k) engine_.Prepare();
    }
    return out;
  }

 private:
  std::vector<uint8_t> main_;
  std::vector<uint8_t> ccm_;
  monsoon::Engine engine_;
  bool ok_;
};

// ---------------------------------------------------------------------------
// Test signals.

inline std::vector<FloatFrame> Silence(float seconds) {
  return std::vector<FloatFrame>(size_t(seconds * 48000) & ~size_t(31));
}

inline std::vector<FloatFrame> Sine(float hz, float seconds, float amp = 0.5f) {
  std::vector<FloatFrame> v(size_t(seconds * 48000) & ~size_t(31));
  for (size_t i = 0; i < v.size(); ++i) {
    float x = amp * std::sin(2.0 * M_PI * hz * i / 48000.0);
    v[i].l = v[i].r = x;
  }
  return v;
}

// A small "piece of music": plucked saw notes, noise hats, a kick, stereo.
inline std::vector<FloatFrame> Music(float seconds, uint32_t seed = 1) {
  std::vector<FloatFrame> v(size_t(seconds * 48000) & ~size_t(31));
  monsoon::Random rng;
  rng.Seed(seed);
  const float notes[] = { 110.0f, 130.81f, 164.81f, 196.0f, 220.0f, 261.63f };
  double phase = 0.0, freq = 110.0, env = 0.0, kick_phase = 0, kick_env = 0;
  double hat_env = 0;
  float lp = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i % 12000 == 0) {
      freq = notes[rng.Next() % 6];
      env = 1.0;
    }
    if (i % 24000 == 0) { kick_env = 1.0; kick_phase = 0; }
    if (i % 6000 == 3000) hat_env = 1.0;
    phase += freq / 48000.0;
    if (phase >= 1.0) phase -= 1.0;
    float saw = float(2.0 * phase - 1.0);
    lp += 0.15f * (saw - lp);
    env *= 0.99985;
    kick_env *= 0.9995;
    hat_env *= 0.998;
    kick_phase += (45.0 + 120.0 * kick_env * kick_env) / 48000.0;
    float kick = float(std::sin(2 * M_PI * kick_phase) * kick_env);
    float hat = rng.Bipolar() * float(hat_env) * 0.15f;
    float s = lp * float(env) * 0.4f;
    v[i].l = s * 0.8f + kick * 0.4f + hat * 0.6f;
    v[i].r = s * 0.6f + kick * 0.4f - hat;
  }
  return v;
}

inline std::vector<FloatFrame> Noise(float seconds, float amp = 0.3f) {
  std::vector<FloatFrame> v(size_t(seconds * 48000) & ~size_t(31));
  monsoon::Random rng;
  rng.Seed(7);
  for (auto& f : v) { f.l = rng.Bipolar() * amp; f.r = rng.Bipolar() * amp; }
  return v;
}

// ---------------------------------------------------------------------------
// Metrics.

struct Stats {
  float peak = 0.0f;
  float rms = 0.0f;
  float dc = 0.0f;
  bool finite = true;
};

inline Stats Measure(const std::vector<FloatFrame>& v, size_t from = 0,
                     size_t to = 0) {
  Stats s;
  if (to == 0 || to > v.size()) to = v.size();
  double sum2 = 0, sum = 0;
  for (size_t i = from; i < to; ++i) {
    for (float x : {v[i].l, v[i].r}) {
      if (!std::isfinite(x)) { s.finite = false; continue; }
      if (std::fabs(x) > s.peak) s.peak = std::fabs(x);
      sum2 += double(x) * x;
      sum += x;
    }
  }
  size_t n = (to - from) * 2;
  if (n) {
    s.rms = float(std::sqrt(sum2 / n));
    s.dc = float(sum / n);
  }
  return s;
}

inline float Db(float x) { return 20.0f * std::log10(x + 1e-12f); }

// Energy of a signal near one frequency (Goertzel), normalised amplitude.
inline float ToneAmplitude(const std::vector<FloatFrame>& v, float hz,
                           size_t from, size_t to, bool left = true) {
  double w = 2 * M_PI * hz / 48000.0, c = 2 * std::cos(w), s1 = 0, s2 = 0;
  for (size_t i = from; i < to; ++i) {
    double x = left ? v[i].l : v[i].r;
    double hann = 0.5 - 0.5 * std::cos(2 * M_PI * (i - from) / double(to - from));
    double s0 = x * hann + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  double power = s1 * s1 + s2 * s2 - c * s1 * s2;
  return float(std::sqrt(power) / ((to - from) * 0.25));
}

// ---------------------------------------------------------------------------
// Tiny test registry.

struct TestCase {
  const char* name;
  std::function<bool()> fn;
};

std::vector<TestCase>& Registry();

struct Registrar {
  Registrar(const char* name, std::function<bool()> fn) {
    Registry().push_back({name, fn});
  }
};

#define MONSOON_TEST(name)                                  \
  static bool name();                                       \
  static host::Registrar registrar_##name(#name, name);     \
  static bool name()

#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("    FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
      std::printf(__VA_ARGS__);                                   \
      std::printf("\n");                                          \
      return false;                                               \
    }                                                             \
  } while (0)

}  // namespace host

#endif  // MONSOON_HOST_HARNESS_H_
