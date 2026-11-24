// BLOOM mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/bloom.h"

using namespace host;
using namespace monsoon;

namespace {

// Silence, then a 330 Hz note with an instant attack at `at`.
std::vector<FloatFrame> Note(float seconds, size_t at, size_t length) {
  std::vector<FloatFrame> v = Silence(seconds);
  for (size_t i = 0; i < length && at + i < v.size(); ++i) {
    float env = std::exp(-float(i) / 12000.0f);
    v[at + i].l = v[at + i].r =
        0.5f * env * std::sin(2 * M_PI * 330.0 * i / 48000.0);
  }
  return v;
}

}  // namespace

MONSOON_TEST(bloom_reverse_swell_rises_before_the_delayed_dry) {
  Module m;
  CHECK(m.SetMode(MODE_BLOOM), "init");
  const size_t at = 24000;
  auto in = Note(2.0f, at, 14400);
  auto out = m.Render(in, [](size_t) {
    Controls c = DefaultControls();
    c.size = 0.5f;
    c.position = 0.0f;
    c.density = 1.0f;
    c.texture = 0.0f;
    return c;
  });
  const size_t w = size_t(BloomMode::WindowSamples(0.5f));
  const size_t onset = at + 2 * w;  // where the delayed dry starts
  float early = Measure(out, onset - w, onset - w / 2).rms;
  float late = Measure(out, onset - w / 2, onset - 480).rms;
  float before = Measure(out, at - 4800, at).rms;
  std::printf("    W = %.0f ms; before input %.1f dB, pre-swell %.1f -> %.1f dB\n",
              w / 48.0, Db(before), Db(early), Db(late));
  CHECK(Db(before) < -80.0f, "noise before the note");
  CHECK(Db(late) > -40.0f, "no reverse bloom before the onset");
  CHECK(late > 1.5f * early, "bloom should rise towards the onset");
  return true;
}

MONSOON_TEST(bloom_softens_attacks) {
  float attack[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_BLOOM), "init");
    const size_t at = 24000;
    auto in = Note(1.5f, at, 24000);
    auto out = m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.size = 0.0f;
      c.position = k == 0 ? 0.0f : 0.8f;
      c.density = 0.0f;
      return c;
    });
    const size_t onset = at + 2 * size_t(BloomMode::WindowSamples(0.0f));
    attack[k] = Measure(out, onset, onset + 960).rms;
  }
  std::printf("    first 20 ms of the note: %.1f dB untouched, %.1f dB with swell\n",
              Db(attack[0]), Db(attack[1]));
  CHECK(attack[1] < attack[0] * 0.5f, "attack not softened");
  return true;
}

MONSOON_TEST(bloom_dry_is_delayed_by_twice_the_window) {
  Module m;
  CHECK(m.SetMode(MODE_BLOOM), "init");
  const size_t at = 24000;
  auto in = Note(1.5f, at, 4800);
  auto out = m.Render(in, [](size_t) {
    Controls c = DefaultControls();
    c.size = 1.0f;
    c.density = 0.0f;
    c.position = 0.0f;
    return c;
  });
  size_t first = 0;
  for (size_t i = at; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > 0.05f) { first = i; break; }
  }
  float expected = 2.0f * BloomMode::WindowSamples(1.0f);
  std::printf("    dry arrives after %.1f ms (2 W = %.1f ms)\n",
              (first - at) / 48.0, expected / 48.0);
  CHECK(std::fabs(float(first - at) - expected) < 960.0f, "latency");
  return true;
}

MONSOON_TEST(bloom_freeze_keeps_blooming) {
  Module m;
  CHECK(m.SetMode(MODE_BLOOM), "init");
  auto in = Note(4.0f, 9600, 30000);
  auto out = m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.size = 0.4f;
    c.density = 1.0f;
    c.freeze = b > 900;  // during the note
    return c;
  });
  float a = Measure(out, 48000 * 2, 48000 * 3).rms;
  float b = Measure(out, 48000 * 3, 48000 * 4).rms;
  std::printf("    frozen bloom: %.1f dB at 2-3 s, %.1f dB at 3-4 s\n", Db(a), Db(b));
  CHECK(Db(a) > -40.0f && std::fabs(Db(a) - Db(b)) < 3.0f, "freeze");
  return true;
}
