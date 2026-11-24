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

MONSOON_TEST(shimmer_bal_fbk_rvb_do_what_they_say) {
  // BAL in the octave set: left takes the climb out, right adds the fall.
  auto in = SineThenSilence(440.0f, 0.5f, 3.0f);
  float band[3][4];  // [neutral, left, right][220, 440, 880, total]
  const float bals[3] = { 0.5f, 0.0f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    auto out = RenderShimmer(in, [&](size_t, Controls* c) {
      c->position = 0.25f;  // octave set
      c->bal = bals[k];
    });
    const size_t a = 48000 * 3 / 2, b = 48000 * 3 - 64;
    band[k][0] = BandRms(out, 220.0f, a, b);
    band[k][1] = BandRms(out, 440.0f, a, b);
    band[k][2] = BandRms(out, 880.0f, a, b);
    band[k][3] = Measure(out, a, b).rms;
    CHECK(Measure(out).finite && Measure(out).peak < 1.0f, "bounded");
  }
  float climb[3], fall[3];
  for (int k = 0; k < 3; ++k) {
    climb[k] = Db(band[k][2]) - Db(band[k][1]);
    fall[k] = Db(band[k][0]) - Db(band[k][3]);
  }
  std::printf("    880 re 440 Hz: noon %+.1f, BAL left %+.1f dB | 220 Hz re all: noon %+.1f,"
              " BAL right %+.1f dB\n", climb[0], climb[1], fall[0], fall[2]);
  CHECK(climb[0] - climb[1] > 10.0f, "BAL left should take the climb out");
  CHECK(fall[2] - fall[0] > 10.0f, "BAL right should make the cascade fall too");

  // FBK: tank modulation spreads a steady 1 kHz tone into sidebands.
  auto tone = Sine(1000.0f, 6.0f, 0.3f);
  float line[2];
  for (int k = 0; k < 2; ++k) {
    auto out = RenderShimmer(tone, [&](size_t, Controls* c) {
      c->position = 0.0f;  // plain
      c->density = 0.5f;
      c->fbk = k == 0 ? 0.0f : 1.0f;
    });
    const size_t a = 48000, b = 48000 * 6 - 64;
    line[k] = ToneAmplitude(out, 1000.0f, a, b) / Measure(out, a, b).rms;
  }
  std::printf("    1 kHz line re total: %.1f dB still, %.1f dB with FBK\n",
              Db(line[0]), Db(line[1]));
  CHECK(Db(line[0]) - Db(line[1]) > 6.0f, "FBK should modulate the tank");

  // RVB: pre-delay moves the first arrival of a click by 250 ms (the
  // click comes 2 s in, after the pre-delay has glided to its setting).
  size_t onset[2];
  const size_t at = 96000;
  for (int k = 0; k < 2; ++k) {
    auto click = Silence(3.0f);
    for (int i = 0; i < 16; ++i) click[at + i].l = click[at + i].r = 0.9f;
    auto out = RenderShimmer(click, [&](size_t, Controls* c) {
      c->position = 0.0f;
      c->rvb = k == 0 ? 0.0f : 1.0f;
    });
    onset[k] = 0;
    for (size_t i = at; i < out.size(); ++i) {
      if (std::fabs(out[i].l) + std::fabs(out[i].r) > 0.01f) {
        onset[k] = i - at;
        break;
      }
    }
  }
  std::printf("    first arrival: %.1f ms, with RVB %.1f ms\n", onset[0] / 48.0f,
              onset[1] / 48.0f);
  CHECK(std::fabs(float(onset[1]) - float(onset[0]) - 12000.0f) < 200.0f,
        "RVB should pre-delay by 250 ms");
  return true;
}
