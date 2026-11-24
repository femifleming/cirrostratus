// RESONATOR behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/resonator.h"

using namespace host;
using namespace monsoon;

namespace {

// Frequency of the strongest tone within +/- `cents` of `hz` (2-cent steps).
float PeakNear(const std::vector<FloatFrame>& v, float hz, float cents,
               size_t from, size_t to) {
  float best = 0.0f, best_hz = hz;
  for (float c = -cents; c <= cents; c += 1.0f) {
    float f = hz * std::pow(2.0f, c / 1200.0f);
    float a = ToneAmplitude(v, f, from, to) + ToneAmplitude(v, f, from, to, false);
    if (a > best) { best = a; best_hz = f; }
  }
  return best_hz;
}

Controls Base() {
  Controls c = DefaultControls();
  c.bal = 0.5f;
  c.rvb = 0.0f;
  return c;
}

std::vector<FloatFrame> Pluck(Controls base, float seconds, bool freeze_after = false) {
  Module m;
  m.SetMode(MODE_RESONATOR);
  auto in = Silence(seconds);
  return m.Render(in, [&](size_t b) {
    Controls c = base;
    c.trigger = b == 4;
    c.freeze = freeze_after && b > 4;
    return c;
  });
}

}  // namespace

MONSOON_TEST(resonator_is_in_tune) {
  const float pitches[] = { -12.0f, 0.0f, 7.0f, 19.0f };
  for (float p : pitches) {
    Controls c = Base();
    c.pitch = p;
    c.size = 0.75f;       // harmonic-series tuning: fundamental = root
    c.density = 0.9f;
    c.position = 0.5f;
    c.texture = 0.0f;
    auto out = Pluck(c, 2.0f);
    float root = 65.41f * std::pow(2.0f, p / 12.0f);
    float found = PeakNear(out, root, 40.0f, 48000 / 2, 48000 * 2 - 64);
    float cents = 1200.0f * std::log2(found / root);
    std::printf("    pitch %+5.1f: root %.2f Hz, measured %.2f Hz (%+.1f cents)\n",
                p, root, found, cents);
    CHECK(std::fabs(cents) <= 4.0f, "out of tune by %.1f cents", cents);
  }
  return true;
}

MONSOON_TEST(resonator_trig_plucks_from_silence) {
  auto out = Pluck(Base(), 1.0f);
  Stats s = Measure(out, 0, 24000);
  std::printf("    pluck rms %.1f dBFS\n", Db(s.rms));
  CHECK(Db(s.rms) > -40.0f, "pluck too quiet");
  return true;
}

MONSOON_TEST(resonator_density_sets_decay) {
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = k == 0 ? 0.2f : 0.8f;
    auto out = Pluck(c, 3.0f);
    float head = Measure(out, 0, 4800).rms;
    tail[k] = Measure(out, 48000 * 2, 48000 * 2 + 9600).rms / head;
  }
  std::printf("    level after 2 s: %.1f dB (density 0.2), %.1f dB (density 0.8)\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(tail[1] > tail[0] * 10.0f, "higher density should ring much longer");
  return true;
}

MONSOON_TEST(resonator_freeze_sustains) {
  Controls c = Base();
  c.density = 0.0f;  // shortest decay - freeze must override it
  auto frozen = Pluck(c, 4.0f, true);
  auto free = Pluck(c, 4.0f, false);
  float early = Measure(frozen, 4800, 9600).rms;
  float late = Measure(frozen, 48000 * 3, 48000 * 4 - 64).rms;
  float free_late = Measure(free, 48000 * 3, 48000 * 4 - 64).rms;
  std::printf("    frozen: %.1f dB -> %.1f dB after 3 s; unfrozen tail %.1f dB\n",
              Db(early), Db(late), Db(free_late));
  CHECK(late > early * 0.25f, "freeze should sustain");
  CHECK(free_late < late * 0.01f, "without freeze it should have died away");
  return true;
}

MONSOON_TEST(resonator_freeze_cuts_input) {
  Module m;
  m.SetMode(MODE_RESONATOR);
  auto in = Noise(2.0f, 0.3f);
  auto out = m.Render(in, [](size_t b) {
    Controls c = Base();
    c.density = 0.0f;
    c.freeze = true;  // frozen from the start, nothing ringing
    return c;
  });
  Stats s = Measure(out, 48000, out.size());
  std::printf("    frozen with noise in: %.1f dBFS\n", Db(s.rms));
  CHECK(Db(s.rms) < -60.0f, "input should not reach frozen strings");
  return true;
}

MONSOON_TEST(resonator_jawari_adds_buzz) {
  float bright[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.texture = k == 0 ? 0.0f : 1.0f;
    c.density = 0.8f;
    c.position = 0.3f;
    auto out = Pluck(c, 1.5f);
    // Ratio of first-difference energy to signal energy ~ brightness.
    std::printf("    texture %d: rms %.1f dB\n", k, Db(Measure(out, 4800, 48000).rms));
    double e = 0, d = 0;
    for (size_t i = 4800; i < 48000; ++i) {
      e += out[i].l * out[i].l;
      float diff = out[i].l - out[i - 1].l;
      d += diff * diff;
    }
    bright[k] = float(d / (e + 1e-20));
  }
  std::printf("    brightness index: %.4f clean, %.4f jawari\n", bright[0], bright[1]);
  CHECK(bright[1] > bright[0] * 1.5f, "jawari should brighten the string");
  return true;
}

MONSOON_TEST(resonator_stays_bounded_with_loud_input) {
 for (int pos = 0; pos < 2; ++pos) {
  Module m;
  m.SetMode(MODE_RESONATOR);
  auto in = Noise(6.0f, 0.9f);
  auto out = m.Render(in, [&](size_t b) {
    Controls c = Base();
    c.density = 1.0f;
    c.texture = 1.0f;
    c.position = pos ? 1.0f : 0.0f;
    c.freeze = b > 48000 * 3 / 32;
    return c;
  });
  Stats a = Measure(out, 48000 * 4, 48000 * 5);
  Stats b = Measure(out, 48000 * 5, out.size());
  std::printf("    frozen after loud noise: %.1f dB then %.1f dB\n", Db(a.rms), Db(b.rms));
  CHECK(a.finite && b.finite, "finite");
  CHECK(b.rms < a.rms * 1.5f, "must not keep growing");
 }
  return true;
}

MONSOON_TEST(resonator_frozen_jawari_does_not_creep) {
  Controls c = Base();
  c.texture = 1.0f;
  c.position = 0.0f;
  auto out = Pluck(c, 8.0f, true);
  float a = Measure(out, 48000 * 1, 48000 * 2).rms;
  float b = Measure(out, 48000 * 7, 48000 * 8 - 64).rms;
  std::printf("    frozen buzzing string: %.1f dB at 1 s, %.1f dB at 7 s\n", Db(a), Db(b));
  CHECK(b < a * 1.25f, "the bridge must not pump energy into a frozen string");
  CHECK(b > a * 0.1f, "but it should still sustain");
  return true;
}
