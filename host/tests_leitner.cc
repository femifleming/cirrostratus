// LEITNER mode behaviour.

#include <cmath>
#include <cstdio>
#include <algorithm>

#include "host/harness.h"
#include "monsoon/dsp/modes/leitner.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.position = 0.5f;   // front
  c.size = 0.2f;
  c.density = 0.5f;
  c.texture = 0.0f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Run(const std::vector<FloatFrame>& in,
                            std::function<void(size_t, Controls*)> f,
                            Module* keep = nullptr) {
  Module local;
  Module& m = keep ? *keep : local;
  m.engine().set_mono_bass(false);  // measure the mode, not the output section
  m.SetMode(MODE_LEITNER);
  return m.Render(in, [&](size_t b) {
    Controls c = Base();
    f(b, &c);
    return c;
  });
}

std::vector<FloatFrame> MonoNoise(float seconds) {
  auto v = Noise(seconds);
  for (auto& f : v) f.r = f.l;
  return v;
}

float ChannelRms(const std::vector<FloatFrame>& v, bool left, size_t from, size_t to) {
  double s = 0;
  for (size_t i = from; i < to; ++i) {
    float x = left ? v[i].l : v[i].r;
    s += x * x;
  }
  return float(std::sqrt(s / (to - from)));
}

// Lag (samples) of R relative to L that maximises the cross-correlation.
int BestLag(const std::vector<FloatFrame>& v, size_t from, size_t to) {
  int best = 0;
  double best_c = -1e30;
  for (int lag = -48; lag <= 48; ++lag) {
    double c = 0;
    for (size_t i = from + 64; i < to - 64; ++i) c += v[i].l * v[i + lag].r;
    if (c > best_c) { best_c = c; best = lag; }
  }
  return best;
}

}  // namespace

MONSOON_TEST(leitner_source_on_the_left_is_louder_and_earlier_left) {
  auto in = MonoNoise(1.0f);
  auto out = Run(in, [](size_t, Controls* c) {
    c->position = 0.75f;   // centre azimuth +90 deg (left)
    c->freeze = true;      // stay at the start of the path
  });
  float l = ChannelRms(out, true, 9600, out.size());
  float r = ChannelRms(out, false, 9600, out.size());
  int lag = BestLag(out, 9600, 30000);
  std::printf("    L %.1f dB, R %.1f dB, right ear lags by %d samples\n",
              Db(l), Db(r), lag);
  CHECK(Db(l) - Db(r) > 6.0f, "level difference");
  CHECK(lag >= 28 && lag <= 34, "ITD should be ~0.66 ms");
  return true;
}

MONSOON_TEST(leitner_moving_source_swaps_sides) {
  auto in = MonoNoise(4.0f);
  auto out = Run(in, [](size_t, Controls* c) {
    c->density = 0.43f;  // 0.02 * 200^0.43 = 0.19 Hz: ~5 s per lap
  });
  float most_left = -100, most_right = 100;
  for (size_t w = 0; w + 9600 <= out.size(); w += 9600) {
    float bal = Db(ChannelRms(out, true, w, w + 9600)) -
                Db(ChannelRms(out, false, w, w + 9600));
    if (bal > most_left) most_left = bal;
    if (bal < most_right) most_right = bal;
  }
  std::printf("    balance range over a lap: %+.1f dB .. %+.1f dB\n", most_right, most_left);
  CHECK(most_left > 4.0f && most_right < -4.0f, "source should travel left and right");
  return true;
}

MONSOON_TEST(leitner_size_sets_distance) {
  float level[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(MonoNoise(1.0f), [&](size_t, Controls* c) {
      c->freeze = true;
      c->size = k == 0 ? 0.0f : 1.0f;
    });
    level[k] = Measure(out, 24000).rms;
  }
  std::printf("    near %.1f dB, far %.1f dB\n", Db(level[0]), Db(level[1]));
  CHECK(Db(level[0]) - Db(level[1]) > 10.0f, "far source should be quieter");
  return true;
}

MONSOON_TEST(leitner_doppler_follows_pitch) {
  // A fly-by (circling the listener gives little radial speed). Largest
  // deviation from 1 kHz, measured by counting zero crossings in 0.1 s.
  auto in = Sine(1000.0f, 4.0f, 0.4f);
  float dev[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(in, [&](size_t b, Controls* c) {
      c->pitch = k == 0 ? -48.0f : 0.0f;   // none / physical
      c->size = 0.6f;
      c->density = 0.6f;                   // ~0.5 Hz
      c->trigger = b == 0 || b == 2;       // fly-by path
    });
    dev[k] = 0.0f;
    for (size_t w = 48000; w + 4800 <= out.size(); w += 2400) {
      int crossings = 0;
      for (size_t i = w + 1; i < w + 4800; ++i) {
        if ((out[i - 1].l < 0.0f) != (out[i].l < 0.0f)) ++crossings;
      }
      float hz = crossings / 2.0f / 0.1f;
      dev[k] = std::max(dev[k], std::fabs(hz - 1000.0f));
    }
  }
  std::printf("    largest pitch deviation: no Doppler %.0f Hz, physical %.0f Hz\n",
              dev[0], dev[1]);
  CHECK(dev[0] <= 15.0f, "without Doppler the pitch should stay put");
  CHECK(dev[1] >= 40.0f, "Doppler should bend the pitch");
  return true;
}

MONSOON_TEST(leitner_texture_adds_room_reflections) {
  float late[2];
  for (int k = 0; k < 2; ++k) {
    auto in = Silence(0.5f);
    for (int i = 0; i < 16; ++i) in[4800 + i].l = in[4800 + i].r = 0.8f;
    auto out = Run(in, [&](size_t, Controls* c) {
      c->freeze = true;
      c->texture = k == 0 ? 0.0f : 0.5f;
    });
    // After the direct click (plus its delay), reflections arrive.
    late[k] = Measure(out, 4800 + 600, 4800 + 6000).peak;
  }
  std::printf("    peak 12-125 ms after the click: dry %.1f dB, room %.1f dB\n",
              Db(late[0]), Db(late[1]));
  CHECK(Db(late[1]) - Db(late[0]) > 20.0f, "reflections missing");
  return true;
}

MONSOON_TEST(leitner_trigger_cycles_paths) {
  Module m;
  m.SetMode(MODE_LEITNER);
  auto in = Silence(0.2f);
  int seen[6];
  for (int k = 0; k < 6; ++k) {
    m.Render(in, [&](size_t b) {
      Controls c = Base();
      c.trigger = k > 0 && b == 0;
      return c;
    });
    seen[k] = static_cast<LeitnerMode*>(m.engine().mode_instance(MODE_LEITNER))->path();
  }
  std::printf("    paths: %d %d %d %d %d %d\n", seen[0], seen[1], seen[2], seen[3],
              seen[4], seen[5]);
  for (int k = 0; k < 6; ++k) CHECK(seen[k] == k % 5, "path order");
  // Every path keeps the source finite and audible.
  for (int p = 0; p < 5; ++p) {
    Module mm;
    mm.SetMode(MODE_LEITNER);
    auto out = mm.Render(MonoNoise(2.0f), [&](size_t b) {
      Controls c = Base();
      c.density = 0.8f;
      c.trigger = b < size_t(p) * 2 && (b % 2) == 0;
      return c;
    });
    Stats s = Measure(out, 24000);
    CHECK(s.finite && Db(s.rms) > -40.0f, "path %d", p);
  }
  return true;
}
