// SHIMMER mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

std::vector<FloatFrame> SineThenSilence(float hz, float on, float total) {
  auto v = Sine(hz, on, 0.5f);
  auto s = Silence(total - on);
  v.insert(v.end(), s.begin(), s.end());
  return v;
}

std::vector<FloatFrame> RenderShimmer(const std::vector<FloatFrame>& in,
                                      std::function<void(size_t, Controls*)> f) {
  Module m;
  m.SetMode(MODE_SHIMMER);
  return m.Render(in, [&](size_t b) {
    Controls c = DefaultControls();
    c.density = 0.8f;
    c.texture = 0.8f;
    f(b, &c);
    return c;
  });
}

// RMS of the output in a band around hz (the tail rings on the network's
// own modes near hz, not exactly on it).
float BandRms(const std::vector<FloatFrame>& v, float hz, size_t from, size_t to) {
  Svf f;
  f.Init();
  f.set(hz / 48000.0f, 6.0f);
  double sum = 0;
  for (size_t i = 0; i < to; ++i) {
    float y = f.BpNormalized(0.5f * (v[i].l + v[i].r));
    if (i >= from) sum += double(y) * y;
  }
  return float(std::sqrt(sum / (to - from)));
}

}  // namespace

MONSOON_TEST(shimmer_octave_set_climbs_an_octave) {
  auto in = SineThenSilence(440.0f, 0.5f, 3.0f);
  float up[2], fund[2];
  for (int k = 0; k < 2; ++k) {
    auto out = RenderShimmer(in, [&](size_t, Controls* c) {
      c->position = k == 0 ? 0.05f : 0.25f;  // plain, octave
    });
    fund[k] = BandRms(out, 440.0f, 48000, 120000);
    up[k] = BandRms(out, 880.0f, 48000, 120000);
    CHECK(Measure(out).finite, "finite");
  }
  std::printf("    plain: 440 %.1f dB, 880 %.1f dB | octave: 440 %.1f dB, 880 %.1f dB\n",
              Db(fund[0]), Db(up[0]), Db(fund[1]), Db(up[1]));
  // Relative to the fundamental, the octave set must lift 880 Hz a lot.
  float lift = (Db(up[1]) - Db(fund[1])) - (Db(up[0]) - Db(fund[0]));
  std::printf("    octave lift relative to plain: %.1f dB\n", lift);
  CHECK(lift > 20.0f, "octave set should add 880 Hz");
  CHECK(up[1] > fund[1], "octave should dominate the tail");
  return true;
}

MONSOON_TEST(shimmer_fifth_set_adds_a_fifth) {
  auto in = SineThenSilence(400.0f, 0.5f, 3.0f);
  float fifth[2], fund[2];
  for (int k = 0; k < 2; ++k) {
    auto out = RenderShimmer(in, [&](size_t, Controls* c) {
      c->position = k == 0 ? 0.05f : 0.42f;
    });
    fifth[k] = BandRms(out, 600.0f, 48000, 120000);
    fund[k] = BandRms(out, 400.0f, 48000, 120000);
  }
  float lift = (Db(fifth[1]) - Db(fund[1])) - (Db(fifth[0]) - Db(fund[0]));
  std::printf("    fifth lift relative to plain: %.1f dB\n", lift);
  CHECK(lift > 15.0f, "no fifth");
  return true;
}

MONSOON_TEST(shimmer_freeze_sustains_and_trig_clears) {
  auto in = Music(1.0f);
  auto s = Silence(5.0f);
  in.insert(in.end(), s.begin(), s.end());
  const size_t trig_block = size_t(5.0f * 48000) / kBlockSize;
  auto out = RenderShimmer(in, [&](size_t b, Controls* c) {
    c->position = 0.05f;
    c->freeze = b * kBlockSize >= 44000;
    c->trigger = b == trig_block;
  });
  float early = Measure(out, 48000, 96000).rms;
  float late = Measure(out, 4 * 48000, 5 * 48000 - 4800).rms;
  float after = Measure(out, 5 * 48000 + 9600, out.size()).rms;
  std::printf("    frozen rms: 1-2 s %.1f dB, 4-5 s %.1f dB; after TRIG %.1f dB\n",
              Db(early), Db(late), Db(after));
  CHECK(Db(late) > Db(early) - 10.0f, "freeze should hold the tail");
  CHECK(Db(after) < -80.0f, "TRIG should clear the tail");
  return true;
}

MONSOON_TEST(shimmer_decays_without_freeze) {
  auto in = SineThenSilence(330.0f, 0.5f, 6.0f);
  auto out = RenderShimmer(in, [&](size_t, Controls* c) {
    c->position = 0.25f;
    c->density = 0.4f;
  });
  float late = Measure(out, 5 * 48000, out.size()).rms;
  std::printf("    rms at 5-6 s with density 0.4: %.1f dB\n", Db(late));
  CHECK(Db(late) < -60.0f, "tail should die away");
  return true;
}
