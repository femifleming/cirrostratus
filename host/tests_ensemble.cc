// ENSEMBLE mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.position = 0.5f;   // ensemble
  c.density = 0.7f;
  c.size = 0.5f;
  c.texture = 0.0f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Run(Module& m, const std::vector<FloatFrame>& in,
                            std::function<void(size_t, Controls*)> f) {
  return m.Render(in, [&](size_t b) {
    Controls c = Base();
    f(b, &c);
    return c;
  });
}

float Correlation(const std::vector<FloatFrame>& v, size_t from) {
  double lr = 0, ll = 0, rr = 0;
  for (size_t i = from; i < v.size(); ++i) {
    lr += v[i].l * v[i].r;
    ll += v[i].l * v[i].l;
    rr += v[i].r * v[i].r;
  }
  return float(lr / std::sqrt(ll * rr + 1e-30));
}

}  // namespace

MONSOON_TEST(ensemble_modulates_and_freeze_stops_it) {
  // Moving delays smear a pure tone into sidebands; frozen LFOs leave it
  // a pure tone again.
  auto in = Sine(440.0f, 3.0f, 0.4f);
  float pure[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_ENSEMBLE);
    auto out = Run(m, in, [&](size_t, Controls* c) {
      c->position = 1.0f;  // vibrato: no dry
      c->freeze = k == 1;
    });
    float tone = ToneAmplitude(out, 440.0f, 48000, out.size());
    float total = Measure(out, 48000).rms * std::sqrt(2.0f);
    pure[k] = tone / total;
  }
  std::printf("    share of energy on 440 Hz: moving %.2f, frozen %.2f\n", pure[0], pure[1]);
  CHECK(pure[1] > 0.95f, "frozen should be a pure delay");
  CHECK(pure[0] < 0.8f, "vibrato should modulate");
  return true;
}

MONSOON_TEST(ensemble_is_wide) {
  Module m;
  m.SetMode(MODE_ENSEMBLE);
  auto in = Noise(2.0f);
  for (auto& f : in) f.r = f.l;  // mono in
  auto out = Run(m, in, [](size_t, Controls* c) { c->position = 0.5f; });
  float corr = Correlation(out, 24000);
  std::printf("    L/R correlation of a mono input: %.2f\n", corr);
  CHECK(corr < 0.9f, "ensemble should widen a mono source");
  return true;
}

MONSOON_TEST(ensemble_size_sets_the_base_delay) {
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_ENSEMBLE);
    auto in = Silence(0.5f);
    for (int i = 0; i < 4; ++i) in[9600 + i].l = in[9600 + i].r = 0.8f;
    auto out = Run(m, in, [&](size_t, Controls* c) {
      c->position = 1.0f;   // single tap, no dry
      c->density = 0.0f;    // static
      c->size = k == 0 ? 0.0f : 1.0f;
    });
    size_t peak = 9600;
    for (size_t i = 9600; i < out.size(); ++i) {
      if (std::fabs(out[i].l) > std::fabs(out[peak].l)) peak = i;
    }
    float ms = (peak - 9600) / 48.0f;
    float expected = k == 0 ? 5.0f : 15.0f;
    std::printf("    size %d: echo after %.2f ms (expected %.0f ms)\n", k, ms, expected);
    CHECK(std::fabs(ms - expected) < 0.2f, "base delay");
  }
  return true;
}

MONSOON_TEST(ensemble_texture_darkens) {
  float level[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_ENSEMBLE);
    auto in = Sine(10000.0f, 1.0f, 0.3f);
    auto out = Run(m, in, [&](size_t, Controls* c) {
      c->position = 1.0f;
      c->texture = k == 0 ? 0.0f : 1.0f;
    });
    level[k] = Measure(out, 24000).rms;
  }
  std::printf("    10 kHz: clean %.1f dB, dark BBD %.1f dB\n", Db(level[0]), Db(level[1]));
  CHECK(Db(level[0]) - Db(level[1]) > 20.0f, "BBD should be dark");
  return true;
}

MONSOON_TEST(ensemble_trigger_resets_the_lfos) {
  // Two modules with different histories give identical output once both
  // are reset by TRIG and fed the same audio.
  auto warm_a = Silence(0.3f);
  auto warm_b = Silence(0.55f);
  auto in = Noise(1.0f);
  std::vector<FloatFrame> outs[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_ENSEMBLE);
    Run(m, k == 0 ? warm_a : warm_b, [](size_t, Controls*) {});
    outs[k] = Run(m, in, [](size_t b, Controls* c) { c->trigger = b == 0; });
  }
  double err = 0;
  for (size_t i = 4800; i < in.size(); ++i) {
    err += std::fabs(outs[0][i].l - outs[1][i].l);
  }
  err /= in.size() - 4800;
  std::printf("    mean difference after reset: %.2e\n", err);
  CHECK(err < 1e-4, "TRIG should reset the LFO phases");
  return true;
}
