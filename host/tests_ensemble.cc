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

MONSOON_TEST(ensemble_bal_fbk_rvb_do_what_they_say) {
  // BAL: slow vs fast LFO. A 1 kHz sine; the fast (6 Hz) LFO puts
  // sidebands at 1 kHz +/- multiples of 6 Hz (the three taps' 120-degree
  // phases cancel some of them, so four are summed).
  auto sine = Sine(1000.0f, 5.0f, 0.3f);
  float side[3];
  for (int k = 0; k < 3; ++k) {
    Module m;
    m.SetMode(MODE_ENSEMBLE);
    auto out = Run(m, sine, [&](size_t, Controls* c) {
      c->density = 1.0f;
      c->bal = 0.5f * k;
    });
    float carrier = ToneAmplitude(out, 1000.0f, 48000, out.size());
    side[k] = 0.0f;
    for (int h = 1; h <= 4; ++h) {
      side[k] += ToneAmplitude(out, 1000.0f + 6.0f * h, 48000, out.size()) +
          ToneAmplitude(out, 1000.0f - 6.0f * h, 48000, out.size());
    }
    side[k] /= carrier + 1e-9f;
  }
  std::printf("    BAL left/noon/right: 6 Hz sidebands %.1f / %.1f / %.1f dB\n",
              Db(side[0]), Db(side[1]), Db(side[2]));
  CHECK(side[0] < side[1] * 0.3f, "BAL left should leave only the slow LFO");
  CHECK(side[2] > side[1] * 1.5f, "BAL right should deepen the fast LFO");

  // FBK: regeneration. An impulse into static delays (DENSITY 0: the
  // flanger use); the plain ensemble is silent after its taps (~10 ms),
  // with FBK they keep coming back.
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_ENSEMBLE);
    auto in = Silence(1.0f);
    in[4800].l = in[4800].r = 0.8f;
    auto out = Run(m, in, [&](size_t, Controls* c) {
      c->density = 0.0f;
      c->fbk = k ? 1.0f : 0.0f;
    });
    Stats s = Measure(out, 4800 + 2400, 4800 + 9600);   // 50 - 100 ms
    Stats all = Measure(out);
    CHECK(all.finite && all.peak < 1.0f, "regeneration must stay bounded");
    tail[k] = s.rms;
  }
  std::printf("    FBK: 50-100 ms after an impulse %.1f dB -> %.1f dB\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(Db(tail[1]) - Db(tail[0]) > 30.0f, "FBK should regenerate the taps");

  // RVB: the FDN service gives a tail long after the input stopped.
  float late[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_ENSEMBLE);
    auto in = Music(1.0f);
    in.resize(48000 * 3);
    auto out = Run(m, in, [&](size_t, Controls* c) { c->rvb = k ? 0.8f : 0.0f; });
    late[k] = Measure(out, 48000 * 2, 48000 * 3).rms;
  }
  std::printf("    RVB: 1-2 s after the input stops %.1f dB -> %.1f dB\n",
              Db(late[0]), Db(late[1]));
  CHECK(Db(late[1]) - Db(late[0]) > 30.0f && Db(late[1]) > -60.0f, "RVB should add a tail");
  return true;
}
