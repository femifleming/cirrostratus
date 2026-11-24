// PLATES behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/plates.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.reverb = 0.0f;
  return c;
}

std::vector<FloatFrame> Strike(Controls base, float seconds, bool freeze = false) {
  Module m;
  m.SetMode(MODE_PLATES);
  auto in = Silence(seconds);
  return m.Render(in, [&](size_t b) {
    Controls c = base;
    c.trigger = b == 4;
    c.freeze = freeze && b > 4;
    return c;
  });
}

PlatesMode* Plates(Module& m) {
  return static_cast<PlatesMode*>(m.engine().mode_instance(MODE_PLATES));
}

}  // namespace

MONSOON_TEST(plates_strike_rings_at_root) {
  Controls c = Base();
  c.density = 0.7f;
  c.pitch = 12.0f;
  auto out = Strike(c, 1.5f);
  Stats s = Measure(out, 0, 24000);
  float root = 130.81f;
  // The fundamental mode must be among the strongest components.
  float at_root = ToneAmplitude(out, root, 2400, 48000) +
      ToneAmplitude(out, root, 2400, 48000, false);
  float off = ToneAmplitude(out, root * 0.8f, 2400, 48000) +
      ToneAmplitude(out, root * 0.8f, 2400, 48000, false);
  std::printf("    strike rms %.1f dBFS, root %.1f dB vs off-root %.1f dB\n",
              Db(s.rms), Db(at_root), Db(off));
  CHECK(Db(s.rms) > -45.0f, "strike too quiet");
  CHECK(at_root > off * 10.0f, "no energy at the root");
  return true;
}

MONSOON_TEST(plates_size_changes_mode_ratios) {
  float second[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_PLATES);
    Controls c = Base();
    c.size = k == 0 ? 0.0f : 1.0f;
    auto in = Silence(0.1f);
    m.Render(in, [&](size_t) { return c; });
    PlatesMode* p = Plates(m);
    second[k] = p->mode_frequency(1) / p->mode_frequency(0);
    for (int i = 1; i < PlatesMode::kNumModes; ++i) {
      CHECK(p->mode_frequency(i) >= p->mode_frequency(i - 1) * 0.999f,
            "modes not sorted");
    }
  }
  std::printf("    second-mode ratio: %.3f square, %.3f long bar\n", second[0], second[1]);
  CHECK(std::fabs(second[0] - 2.5f) < 0.05f, "square plate (1,2) mode should be 2.5x");
  CHECK(second[1] < 1.5f, "a long bar clusters its low modes");
  return true;
}

MONSOON_TEST(plates_density_sets_decay) {
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = k == 0 ? 0.2f : 0.8f;
    auto out = Strike(c, 3.0f);
    tail[k] = Measure(out, 48000 * 2, 48000 * 2 + 9600).rms /
        Measure(out, 0, 4800).rms;
  }
  std::printf("    level after 2 s: %.1f dB vs %.1f dB\n", Db(tail[0]), Db(tail[1]));
  CHECK(tail[1] > tail[0] * 10.0f, "higher density should ring longer");
  return true;
}

MONSOON_TEST(plates_freeze_rings_forever) {
  Controls c = Base();
  c.density = 0.0f;
  auto out = Strike(c, 4.0f, true);
  float early = Measure(out, 2400, 9600).rms;
  float late = Measure(out, 48000 * 3, 48000 * 4 - 64).rms;
  std::printf("    frozen plate: %.1f dB -> %.1f dB after 3 s\n", Db(early), Db(late));
  CHECK(late > early * 0.5f, "freeze should sustain");
  return true;
}

MONSOON_TEST(plates_wood_loses_highs_faster_than_metal) {
  float hf[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.texture = k == 0 ? 0.0f : 1.0f;
    c.density = 0.6f;
    c.pitch = 24.0f;
    auto out = Strike(c, 1.0f);
    double e = 0, d = 0;
    for (size_t i = 9600; i < 24000; ++i) {
      e += out[i].l * out[i].l;
      float diff = out[i].l - out[i - 1].l;
      d += diff * diff;
    }
    hf[k] = float(d / (e + 1e-20));
  }
  std::printf("    brightness after 0.2 s: metal %.4f, wood %.4f\n", hf[0], hf[1]);
  CHECK(hf[0] > hf[1] * 1.5f, "metal should stay brighter");
  return true;
}

MONSOON_TEST(plates_input_excites_and_stays_bounded) {
  Module m;
  m.SetMode(MODE_PLATES);
  auto in = Noise(4.0f, 0.9f);
  auto out = m.Render(in, [](size_t b) {
    Controls c = Base();
    c.density = 1.0f;
    c.freeze = b > 48000 * 2 / 32;
    c.trigger = (b % 50) == 0;  // hammer it while frozen
    return c;
  });
  Stats a = Measure(out, 0, 48000);
  Stats b = Measure(out, 48000 * 3, out.size());
  std::printf("    noise-driven %.1f dB, hammered frozen %.1f dB\n", Db(a.rms), Db(b.rms));
  CHECK(Db(a.rms) > -50.0f, "input should excite the plate");
  CHECK(b.finite && b.peak <= 1.0f, "bounded");
  return true;
}
