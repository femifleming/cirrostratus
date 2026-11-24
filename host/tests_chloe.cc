// CHLOE (Chloaelis strip) behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

// Finds the strongest echo of an impulse after `skip` samples.
size_t EchoArrival(const std::vector<FloatFrame>& out, size_t skip) {
  size_t best = 0;
  float peak = 0.0f;
  for (size_t i = skip; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > peak) { peak = std::fabs(out[i].l); best = i; }
  }
  return best;
}

}  // namespace

MONSOON_TEST(chloe_every_engine_echoes_at_the_size_time) {
  // SIZE 0.5 -> 20 ms * 2^(3.16) = 179 ms.
  const float expected = 0.02f * exp2f(0.5f * 6.32f) * 48000.0f;
  const char* names[3] = { "PT2399", "BBD", "tape" };
  for (int engine = 0; engine < 3; ++engine) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, engine), "init");
    std::vector<FloatFrame> in = Silence(1.0f);
    for (int i = 0; i < 96; ++i) in[4800 + i].l = in[4800 + i].r = 0.5f * std::sin(i * 0.3f);
    auto out = m.Render(in, [](size_t) {
      Controls c = DefaultControls();
      c.size = 0.5f; c.density = 0.0f; c.position = 0.95f; c.texture = 0.0f;
      return c;
    });
    size_t arrival = EchoArrival(out, 4800 + 2000) - 4800;
    std::printf("    %-6s echo after %.1f ms (expected %.1f ms)\n", names[engine],
                arrival / 48.0, expected / 48.0);
    CHECK(std::fabs(float(arrival) - expected) < 0.06f * expected + 200.0f, "echo time");
  }
  return true;
}

MONSOON_TEST(chloe_string_engine_plays_the_tuned_note) {
  for (float pitch : { -12.0f, 0.0f, 7.0f }) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, 3), "init");
    std::vector<FloatFrame> in = Silence(1.5f);
    auto out = m.Render(in, [&](size_t b) {
      Controls c = DefaultControls();
      c.pitch = pitch; c.size = 0.0f; c.density = 0.95f; c.position = 1.0f;
      c.texture = 0.0f; c.trigger = (b == 10);
      return c;
    });
    float hz = 130.81f * std::pow(2.0f, pitch / 12.0f);
    float at = ToneAmplitude(out, hz, 24000, 60000);
    float off = ToneAmplitude(out, hz * 1.06f, 24000, 60000);
    std::printf("    pitch %+5.1f: %.1f Hz amplitude %.4f, a semitone up %.4f\n",
                pitch, hz, at, off);
    CHECK(at > 4.0f * off && at > 0.002f, "string not at the note");
  }
  return true;
}

MONSOON_TEST(chloe_freeze_holds_the_delay) {
  Module m;
  CHECK(m.SetMode(MODE_CHLOE, 0), "init");
  auto in = Music(4.0f);
  for (size_t i = 48000 * 2; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
  auto out = m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.size = 0.45f; c.density = 0.3f; c.texture = 0.2f; c.position = 0.9f;
    c.freeze = b > 2900;  // just before the input stops
    return c;
  });
  float late = Measure(out, 48000 * 3, 48000 * 4).rms;
  std::printf("    frozen PT2399 loop, 1-2 s after the input stopped: %.1f dBFS\n", Db(late));
  CHECK(Db(late) > -40.0f, "freeze lost the loop");
  return true;
}

MONSOON_TEST(chloe_stays_bounded_at_full_regen_and_drive) {
  for (int option = 0; option < 8; ++option) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, option), "init");
    auto in = Music(6.0f, option);
    auto out = m.Render(in, [](size_t b) {
      Controls c = DefaultControls();
      c.density = 1.0f; c.texture = 1.0f; c.size = (b % 400) / 400.0f;
      c.position = 0.7f; c.fbk = 0.8f; c.trigger = (b % 500) == 0;
      return c;
    });
    Stats s = Measure(out);
    CHECK(s.finite && s.peak <= 1.0f, "option %d unbounded", option);
  }
  return true;
}
