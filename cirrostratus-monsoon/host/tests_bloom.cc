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

// Per-mode knobs: BAL = falling (left) / forward (right) blooms, FBK =
// regeneration (the engine's feedback loop), RVB = haze tail. Measured on a single click: the
// blooms of a click are echoes of it between the click and the delayed dry.
MONSOON_TEST(bloom_bal_fbk_rvb_do_what_they_say) {
  const size_t at = 24000;
  const size_t w = size_t(BloomMode::WindowSamples(0.5f));
  auto run = [&](float bal, float fbk, float rvb, float seconds) {
    Module m;
    m.SetMode(MODE_BLOOM);
    auto in = Silence(seconds);
    in[at].l = in[at].r = 0.8f;
    return m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.size = 0.5f;
      c.position = 0.0f;
      c.density = 1.0f;
      c.texture = 0.0f;
      c.bal = bal;
      c.fbk = fbk;
      c.rvb = rvb;
      return c;
    });
  };
  // Energy-weighted mean echo time (in windows) and the share of the echo
  // energy landing exactly one window after the click.
  auto echoes = [&](const std::vector<FloatFrame>& out, float* centroid,
                    float* at_w) {
    double e = 0, et = 0, ew = 0;
    for (size_t i = at + 16; i < at + 2 * w - 64; ++i) {
      double p = double(out[i].l) * out[i].l + double(out[i].r) * out[i].r;
      e += p;
      et += p * double(i - at) / w;
      if (i + 8 >= at + w && i <= at + w + 8) ew += p;
    }
    *centroid = float(et / (e + 1e-30));
    *at_w = float(ew / (e + 1e-30));
  };
  float cen[3], share[3];
  const float bals[3] = { 0.0f, 0.5f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    auto out = run(bals[k], 0.0f, 0.0f, 1.0f);
    echoes(out, &cen[k], &share[k]);
  }
  std::printf("    BAL: echo centroid left %.2f W, noon %.2f W, right %.2f W; "
              "energy at exactly 1 W: %.0f%% / %.0f%% / %.0f%%\n",
              cen[0], cen[1], cen[2], 100 * share[0], 100 * share[1], 100 * share[2]);
  CHECK(cen[1] > 1.4f, "noon: blooms swell up to the dry");
  CHECK(cen[0] < 0.6f * cen[1], "BAL left: blooms should strike right after the sound");
  CHECK(share[2] > 0.9f && share[1] < 0.1f, "BAL right: forward blooms, one window late");

  // FBK: echoes of echoes keep coming after the delayed dry (2 W).
  float late[2];
  for (int k = 0; k < 2; ++k) {
    auto out = run(0.5f, k ? 1.0f : 0.0f, 0.0f, 1.5f);
    late[k] = Db(Measure(out, at + 2 * w + 480, at + 6 * w).rms);
    CHECK(Measure(out).finite, "finite");
  }
  std::printf("    FBK: 2 W ... 6 W after the click: off %.1f dB, full %.1f dB\n",
              late[0], late[1]);
  CHECK(late[0] < -70.0f, "(almost) nothing after the dry without FBK");
  CHECK(late[1] > late[0] + 20.0f, "FBK should make the blooms bloom again");

  // RVB: the blooms ring on after everything else has finished (the
  // last bloom of the click ends with the delayed dry at 2 W).
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    auto out = run(0.5f, 0.0f, k ? 1.0f : 0.0f, 2.5f);
    tail[k] = Db(Measure(out, at + 2 * w + 2400, at + 2 * w + 26400).rms);
    CHECK(Measure(out).finite, "finite");
  }
  std::printf("    RVB: 50-550 ms after the delayed dry: off %.1f dB, full %.1f dB\n",
              tail[0], tail[1]);
  CHECK(tail[0] < -70.0f && tail[1] > tail[0] + 20.0f, "RVB should add a haze tail");

  // RVB on music stays near the dry bloom's level.
  {
    float lv[2];
    for (int k = 0; k < 2; ++k) {
      Module m;
      m.SetMode(MODE_BLOOM);
      auto out = m.Render(Music(4.0f), [&](size_t) {
        Controls c = DefaultControls();
        c.density = 1.0f;
        c.texture = 0.0f;
        c.rvb = k ? 1.0f : 0.0f;
        return c;
      });
      lv[k] = Db(Measure(out, 48000).rms);
    }
    std::printf("    RVB on music: %.1f -> %.1f dB\n", lv[0], lv[1]);
    CHECK(lv[1] - lv[0] < 6.0f, "RVB level");
  }

  // Full FBK on loud music, then random fast CV: bounded.
  {
    Module m;
    m.SetMode(MODE_BLOOM);
    Random r;
    r.Seed(9);
    auto out = m.Render(Music(6.0f), [&](size_t b) {
      Controls c = DefaultControls();
      c.density = 1.0f;
      c.texture = b < 4000 ? 1.0f : r.Float();
      c.fbk = b < 4000 ? 1.0f : r.Float();
      c.bal = b < 4000 ? 0.0f : r.Float();
      c.rvb = b < 4000 ? 0.0f : r.Float();
      c.size = b < 4000 ? 0.0f : r.Float();
      return c;
    });
    Stats st = Measure(out);
    std::printf("    full FBK + random CV: peak %.2f, rms %.1f dB\n", st.peak, Db(st.rms));
    CHECK(st.finite && st.peak < 2.0f, "FBK should stay bounded");
  }
  return true;
}
