// SPRING (mode 1, new in 1.5.2): a dispersive spring tank.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/spring.h"

using namespace host;
using namespace monsoon;

static std::vector<FloatFrame> Click(float seconds, size_t at) {
  auto v = Silence(seconds);
  v[at].l = v[at].r = 0.9f;
  return v;
}

// Energy-weighted arrival time (ms after the click) of one band of the
// first echo.
static float Arrival(const std::vector<FloatFrame>& out, size_t from,
                     size_t to, float hz) {
  Svf f;
  f.Init();
  f.set(hz / 48000.0f, 2.0f);
  double t = 0.0, e = 0.0;
  for (size_t i = from; i < to; ++i) {
    const float y = f.Bp(out[i].l + out[i].r);
    t += double(y) * y * (i - from);
    e += double(y) * y;
  }
  return float(t / (e + 1e-30) / 48.0);
}

MONSOON_TEST(spring_echoes_are_falling_chirps) {
  float spread[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_SPRING), "init");
    const size_t at = 4800;
    auto out = m.Render(Click(1.0f, at), [k](size_t) {
      Controls c = DefaultControls();
      c.texture = k ? 1.0f : 0.0f;
      c.position = 1.0f;   // bright: let the highs through
      return c;
    });
    const size_t trip = static_cast<size_t>(
        static_cast<SpringMode*>(m.engine().mode_instance(MODE_SPRING))
            ->trip_samples(0));
    // the first echo: from the trip to a trip and a half later
    const float lo = Arrival(out, at + trip / 2, at + trip * 2, 300.0f);
    const float hi = Arrival(out, at + trip / 2, at + trip * 2, 3000.0f);
    spread[k] = lo - hi;
    std::printf("    TEXTURE %d: 300 Hz arrives %.1f ms after 3 kHz\n", k, spread[k]);
    CHECK(Measure(out).finite, "finite");
  }
  CHECK(spread[1] > 6.0f, "full dispersion: lows %.1f ms behind", spread[1]);
  CHECK(spread[1] > 2.5f * spread[0], "TEXTURE widens the chirp (%.1f -> %.1f ms)",
        spread[0], spread[1]);
  return true;
}

MONSOON_TEST(spring_density_sets_the_decay) {
  float drop[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_SPRING), "init");
    auto in = Noise(0.3f);
    auto tail = Silence(2.0f);
    in.insert(in.end(), tail.begin(), tail.end());
    auto out = m.Render(in, [k](size_t) {
      Controls c = DefaultControls();
      c.density = k ? 1.0f : 0.0f;
      return c;
    });
    const Stats a = Measure(out, 24000, 28800);     // 0.2 s after
    const Stats b = Measure(out, 52800, 57600);     // 0.8 s after
    drop[k] = Db(a.rms) - Db(b.rms);
    std::printf("    DENSITY %d: %.1f dB down in 0.6 s\n", k, drop[k]);
  }
  CHECK(drop[0] > 30.0f, "short decay (T60 0.4 s): %.1f dB", drop[0]);
  CHECK(drop[1] < 8.0f, "long decay (T60 8 s): %.1f dB", drop[1]);
  return true;
}

MONSOON_TEST(spring_freeze_rings_on) {
  Module m;
  CHECK(m.SetMode(MODE_SPRING), "init");
  auto in = Noise(5.0f);
  auto out = m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.freeze = b > 1500;   // 1 s of noise, then held
    return c;
  });
  const Stats a = Measure(out, 48000 * 1 + 9600, 48000 * 2);
  const Stats b = Measure(out, 48000 * 4, 48000 * 5);
  std::printf("    frozen: %.1f dB, 3 s later %.1f dB\n", Db(a.rms), Db(b.rms));
  CHECK(b.finite && b.peak < 2.0f, "bounded (peak %.2f)", b.peak);
  CHECK(std::fabs(Db(a.rms) - Db(b.rms)) < 6.0f, "held");
  return true;
}

MONSOON_TEST(spring_trig_kicks_the_tank) {
  Module m;
  CHECK(m.SetMode(MODE_SPRING), "init");
  auto out = m.Render(Silence(1.5f), [](size_t b) {
    Controls c = DefaultControls();
    c.trigger = b == 300;
    return c;
  });
  const Stats before = Measure(out, 0, 300 * 32);
  const Stats after = Measure(out, 300 * 32, 300 * 32 + 24000);
  std::printf("    knock: %.1f dB (silent before: %.1f dB)\n", Db(after.rms), Db(before.rms));
  CHECK(Db(after.rms) > -40.0f && Db(before.rms) < -100.0f, "the crash");
  return true;
}

MONSOON_TEST(spring_pitch_is_tension) {
  Module m;
  CHECK(m.SetMode(MODE_SPRING), "init");
  SpringMode* s = static_cast<SpringMode*>(m.engine().mode_instance(MODE_SPRING));
  float trip[2];
  for (int k = 0; k < 2; ++k) {
    m.Render(Silence(0.5f), [k](size_t) {
      Controls c = DefaultControls();
      c.pitch = k ? 12.0f : 0.0f;
      return c;
    });
    trip[k] = s->trip_samples(0);
  }
  CHECK(std::fabs(trip[1] / trip[0] - 0.5f) < 0.01f,
        "an octave up halves the trip (%.0f -> %.0f)", trip[0], trip[1]);
  return true;
}
