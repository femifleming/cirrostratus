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
  c.rvb = 0.0f;
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

// Frequency of the strongest component in [lo, hi] within [from, to).
static float PeakHz(const std::vector<FloatFrame>& v, float lo, float hi,
                    size_t from, size_t to) {
  float best = lo, best_a = -1.0f;
  for (float hz = lo; hz <= hi; hz += 0.25f) {
    float a = ToneAmplitude(v, hz, from, to);
    if (a > best_a) { best_a = a; best = hz; }
  }
  return best;
}

MONSOON_TEST(plates_bal_fbk_rvb_do_what_they_say) {
  // BAL: left snaps the modes onto harmonics, right stretches the series.
  float worst_off = 0.0f, second[3];
  const float bals[3] = { 0.5f, 0.0f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    Module m;
    m.SetMode(MODE_PLATES);
    Controls c = Base();
    c.size = 0.0f;  // square plate: (1,2) at 2.5 x
    c.bal = bals[k];
    m.Render(Silence(0.1f), [&](size_t) { return c; });
    PlatesMode* p = Plates(m);
    second[k] = p->mode_frequency(1) / p->mode_frequency(0);
    if (k == 1) {
      for (int i = 0; i < PlatesMode::kNumModes; ++i) {
        float r = p->mode_frequency(i) / p->mode_frequency(0);
        worst_off = std::fmax(worst_off, std::fabs(r - std::round(r)));
      }
    }
  }
  std::printf("    (1,2) mode: noon %.3f, left %.3f, right %.3f x root; left: worst"
              " distance from a harmonic %.4f\n", second[0], second[1], second[2], worst_off);
  CHECK(worst_off < 0.01f, "BAL left should tune every mode to a harmonic");
  CHECK(second[2] > second[0] * 1.6f, "BAL right should stretch the series");

  // FBK: tension. Right after a strike the root rings sharp, later it is
  // back at pitch.
  float early[2], late[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = 0.8f;
    c.pitch = 12.0f;  // root 130.8 Hz
    c.fbk = k == 0 ? 0.0f : 1.0f;
    auto out = Strike(c, 3.0f);
    const size_t s = 4 * kBlockSize;
    early[k] = PeakHz(out, 120.0f, 165.0f, s + 480, s + 4800);
    late[k] = PeakHz(out, 120.0f, 165.0f, s + 96000, s + 120000);
    CHECK(Measure(out).finite && Measure(out).peak < 1.0f, "bounded");
  }
  std::printf("    root after the strike: noon %.1f -> %.1f Hz, FBK %.1f -> %.1f Hz\n",
              early[0], late[0], early[1], late[1]);
  CHECK(12.0f * std::log2(early[1] / early[0]) > 1.0f, "FBK: a hard hit rings sharp");
  CHECK(std::fabs(12.0f * std::log2(late[1] / late[0])) < 0.5f, "FBK: and glides back");

  // RVB = FDN: a short plate gets a tail.
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = 0.0f;  // 0.3 s plate
    c.rvb = k == 0 ? 0.0f : 0.8f;
    auto out = Strike(c, 2.5f);
    tail[k] = Measure(out, 48000 * 3 / 2, 48000 * 2).rms;
  }
  std::printf("    1.5-2 s after the strike: %.1f dB dry, %.1f dB with RVB\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(Db(tail[1]) - Db(tail[0]) > 30.0f, "RVB should add a reverb tail");
  return true;
}
