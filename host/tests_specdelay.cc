// SPECDELAY mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

// A short burst holding a low and a high tone at once.
std::vector<FloatFrame> TwoToneBurst(float seconds, size_t at) {
  std::vector<FloatFrame> v = Silence(seconds);
  for (size_t i = 0; i < 960; ++i) {
    float w = std::sin(M_PI * i / 960.0);
    float x = 0.3f * w * (std::sin(2 * M_PI * 300.0 * i / 48000.0) +
                          std::sin(2 * M_PI * 6000.0 * i / 48000.0));
    v[at + i].l = v[at + i].r = x;
  }
  return v;
}

// Time (samples) where a tone's energy peaks.
size_t PeakTime(const std::vector<FloatFrame>& v, float hz, size_t from) {
  size_t best = from;
  float peak = 0.0f;
  for (size_t i = from; i + 1024 < v.size(); i += 256) {
    float a = ToneAmplitude(v, hz, i, i + 1024);
    if (a > peak) { peak = a; best = i + 512; }
  }
  return best;
}

Controls Neutral() {
  Controls c = DefaultControls();
  c.density = 0.0f;
  c.texture = 0.5f;
  c.pitch = 0.0f;
  return c;
}

}  // namespace

MONSOON_TEST(specdelay_position_sets_which_band_returns_last) {
  float diff[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_SPECDELAY), "init");
    auto in = TwoToneBurst(1.5f, 4800);
    auto out = m.Render(in, [&](size_t) {
      Controls c = Neutral();
      c.position = k == 0 ? 0.0f : 1.0f;
      c.size = 1.0f;
      return c;
    });
    size_t low = PeakTime(out, 300.0f, 4800);
    size_t high = PeakTime(out, 6000.0f, 4800);
    diff[k] = (float(high) - float(low)) / 48.0f;
    std::printf("    position %d: 300 Hz back at %.0f ms, 6 kHz at %.0f ms\n",
                k, (low - 4800) / 48.0, (high - 4800) / 48.0);
  }
  CHECK(diff[0] < -200.0f, "position 0 should bring the lows back last");
  CHECK(diff[1] > 200.0f, "position 1 should bring the highs back last");
  return true;
}

MONSOON_TEST(specdelay_flat_curve_is_a_clean_delay) {
  Module m;
  CHECK(m.SetMode(MODE_SPECDELAY), "init");
  auto in = TwoToneBurst(1.0f, 4800);
  auto out = m.Render(in, [](size_t) {
    Controls c = Neutral();
    c.position = 0.5f;
    c.size = 0.5f;
    return c;
  });
  size_t low = PeakTime(out, 300.0f, 4800);
  size_t high = PeakTime(out, 6000.0f, 4800);
  float a_in = ToneAmplitude(in, 300.0f, 4800, 5824);
  float a_out = ToneAmplitude(out, 300.0f, low - 512, low + 512);
  std::printf("    both bands back at %.0f / %.0f ms, level %.1f dB\n",
              (low - 4800) / 48.0, (high - 4800) / 48.0, Db(a_out / a_in));
  CHECK(std::fabs(float(low) - float(high)) < 600.0f, "bands should align");
  CHECK(std::fabs(Db(a_out / a_in)) < 3.0f, "level");
  return true;
}

MONSOON_TEST(specdelay_freeze_holds_and_trig_clears) {
  Module m;
  CHECK(m.SetMode(MODE_SPECDELAY), "init");
  auto in = Noise(5.0f, 0.2f);
  for (size_t i = 48000; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
  auto out = m.Render(in, [](size_t b) {
    Controls c = Neutral();
    c.size = 0.6f;
    c.density = 0.5f;
    c.freeze = b > 1350;        // just before the input stops (1 s)
    c.trigger = b == 6000;      // 4 s
    return c;
  });
  float held = Measure(out, 48000 * 2, 48000 * 3).rms;
  float later = Measure(out, 48000 * 3, 48000 * 4 - 4800).rms;
  float cleared = Measure(out, 48000 * 4 + 9600, 48000 * 5).rms;
  std::printf("    frozen: %.1f dB at 1-2 s after, %.1f dB at 2-3 s; after TRIG %.1f dB\n",
              Db(held), Db(later), Db(cleared));
  CHECK(Db(held) > -40.0f && std::fabs(Db(held) - Db(later)) < 3.0f, "freeze");
  CHECK(Db(cleared) < -80.0f, "trig should clear");
  return true;
}

MONSOON_TEST(specdelay_pitch_spirals_the_repeats) {
  Module m;
  CHECK(m.SetMode(MODE_SPECDELAY), "init");
  auto in = Silence(2.0f);
  for (size_t i = 0; i < 2400; ++i) {
    in[4800 + i].l = in[4800 + i].r =
        0.3f * std::sin(2 * M_PI * 1000.0 * i / 48000.0);
  }
  auto out = m.Render(in, [](size_t) {
    Controls c = Neutral();
    c.size = 0.3f;
    c.density = 0.9f;
    c.pitch = 12.0f;
    return c;
  });
  float octave = 0.0f, fourth_harm = 0.0f;
  for (size_t i = 9600; i + 2048 < out.size(); i += 1024) {
    octave = std::max(octave, ToneAmplitude(out, 2000.0f, i, i + 2048));
    fourth_harm = std::max(fourth_harm, ToneAmplitude(out, 4000.0f, i, i + 2048));
  }
  std::printf("    repeats an octave up %.1f dB, two octaves up %.1f dB\n",
              Db(octave), Db(fourth_harm));
  CHECK(Db(octave) > -45.0f && Db(fourth_harm) > -60.0f, "no spiral");
  return true;
}
