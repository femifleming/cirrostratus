// BARBERPOLE mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/barberpole.h"

using namespace host;
using namespace monsoon;

namespace {

// POSITION that gives a +100 Hz shift (0.1 * 20000^((m - 0.02) / 0.98)).
const float kUp100 = 0.5f + 0.5f * (0.02f + 0.98f * 0.697519f);

Controls Base() {
  Controls c = DefaultControls();
  c.position = kUp100;
  c.size = 0.3f;
  c.density = 0.0f;
  c.texture = 0.0f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Run(const std::vector<FloatFrame>& in,
                            std::function<void(size_t, Controls*)> f,
                            Module* keep = nullptr) {
  Module local;
  Module& m = keep ? *keep : local;
  m.SetMode(MODE_BARBERPOLE);
  return m.Render(in, [&](size_t b) {
    Controls c = Base();
    f(b, &c);
    return c;
  });
}

}  // namespace

MONSOON_TEST(barberpole_one_pass_shifts_by_the_set_amount) {
  Module m;
  auto in = Sine(1000.0f, 2.0f, 0.4f);
  auto out = Run(in, [](size_t, Controls*) {}, &m);
  auto* mode = static_cast<BarberpoleMode*>(m.engine().mode_instance(MODE_BARBERPOLE));
  float up = ToneAmplitude(out, 1100.0f, 48000, out.size());
  float mirror = ToneAmplitude(out, 900.0f, 48000, out.size());
  float orig = ToneAmplitude(out, 1000.0f, 48000, out.size());
  std::printf("    shift %.1f Hz: 1100 Hz %.1f dB, 900 Hz %.1f dB, 1000 Hz %.1f dB\n",
              mode->shift_hz(0), Db(up), Db(mirror), Db(orig));
  CHECK(std::fabs(mode->shift_hz(0) - 100.0f) < 1.0f, "shift mapping");
  CHECK(up > 0.1f, "shifted tone missing");
  CHECK(Db(up) - Db(mirror) > 35.0f && Db(up) - Db(orig) > 35.0f, "not clean SSB");
  return true;
}

MONSOON_TEST(barberpole_feedback_builds_the_spiral) {
  auto in = Sine(1000.0f, 3.0f, 0.3f);
  auto out = Run(in, [](size_t, Controls* c) {
    c->density = 0.85f;
    c->size = 0.2f;
  });
  float steps[4];
  for (int k = 0; k < 4; ++k) {
    steps[k] = ToneAmplitude(out, 1100.0f + 100.0f * k, 96000, out.size());
  }
  std::printf("    1100 %.1f, 1200 %.1f, 1300 %.1f, 1400 %.1f dB\n",
              Db(steps[0]), Db(steps[1]), Db(steps[2]), Db(steps[3]));
  for (int k = 0; k < 4; ++k) CHECK(Db(steps[k]) > -40.0f, "step %d missing", k + 1);
  CHECK(Measure(out).finite, "finite");
  return true;
}

MONSOON_TEST(barberpole_trigger_flips_and_texture_splits) {
  auto in = Sine(1000.0f, 3.0f, 0.4f);
  const size_t flip = 48000 / kBlockSize;
  auto flipped = Run(in, [&](size_t b, Controls* c) { c->trigger = b == flip; });
  float down = ToneAmplitude(flipped, 900.0f, 96000, flipped.size());
  float up = ToneAmplitude(flipped, 1100.0f, 96000, flipped.size());
  std::printf("    after TRIG: 900 Hz %.1f dB, 1100 Hz %.1f dB\n", Db(down), Db(up));
  CHECK(Db(down) - Db(up) > 35.0f, "TRIG should flip the direction");

  auto split = Run(in, [](size_t, Controls* c) { c->texture = 1.0f; });
  float l_up = ToneAmplitude(split, 1100.0f, 48000, split.size(), true);
  float r_down = ToneAmplitude(split, 900.0f, 48000, split.size(), false);
  float r_up = ToneAmplitude(split, 1100.0f, 48000, split.size(), false);
  std::printf("    texture 1: L 1100 %.1f dB, R 900 %.1f dB, R 1100 %.1f dB\n",
              Db(l_up), Db(r_down), Db(r_up));
  CHECK(l_up > 0.05f && r_down > 0.05f, "split missing");
  CHECK(Db(r_down) - Db(r_up) > 30.0f, "right should go down");
  return true;
}

MONSOON_TEST(barberpole_size_sets_the_loop_delay) {
  // A click comes back after the loop delay (size 0.5 -> 72 * (24000/72)^0.5).
  auto in = Silence(1.0f);
  for (int i = 0; i < 16; ++i) in[4800 + i].l = in[4800 + i].r = 0.8f;
  auto out = Run(in, [](size_t, Controls* c) {
    c->size = 0.5f;
    c->position = 0.5f;  // no shift
  });
  size_t peak = 4800;
  for (size_t i = 4800; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > std::fabs(out[peak].l)) peak = i;
  }
  float expected = 72.0f * std::sqrt(24000.0f / 72.0f);
  std::printf("    echo after %zu samples (expected ~%.0f)\n", peak - 4800, expected);
  CHECK(std::fabs(float(peak - 4800) - expected) < 40.0f, "loop delay");
  return true;
}

MONSOON_TEST(barberpole_freeze_keeps_spiralling_without_input) {
  auto in = Sine(500.0f, 1.0f, 0.4f);
  auto s = Silence(3.0f);
  in.insert(in.end(), s.begin(), s.end());
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(in, [&](size_t b, Controls* c) {
      c->position = 0.5f + 0.5f * (0.02f + 0.98f * 0.4f);  // ~5 Hz
      c->density = 0.5f;
      c->size = 0.6f;
      c->freeze = k == 1 && b * kBlockSize > 40000;
    });
    tail[k] = Measure(out, 3 * 48000, out.size()).rms;
  }
  std::printf("    rms at 3-4 s: normal %.1f dB, frozen %.1f dB\n", Db(tail[0]), Db(tail[1]));
  CHECK(Db(tail[1]) > -30.0f, "frozen spiral died");
  CHECK(Db(tail[1]) - Db(tail[0]) > 30.0f, "freeze should sustain");
  return true;
}

namespace {

// Energy share of one side below 300 Hz and above 4 kHz.
void Tilt(const std::vector<FloatFrame>& v, size_t from, float* low,
          float* high) {
  Svf lp, hp;
  lp.Init();
  hp.Init();
  lp.set(300.0f / 48000.0f, 0.7f);
  hp.set(4000.0f / 48000.0f, 0.7f);
  double e = 0, el = 0, eh = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    float a = lp.Lp(v[i].l), b = hp.Hp(v[i].l);
    if (i < from) continue;
    e += v[i].l * v[i].l;
    el += a * a;
    eh += b * b;
  }
  *low = float(el / (e + 1e-30));
  *high = float(eh / (e + 1e-30));
}

}  // namespace

MONSOON_TEST(barberpole_bal_fbk_rvb_do_what_they_say) {
  // BAL: the loop's tone. A noise burst recirculating without a shift.
  auto burst = Noise(2.0f, 0.3f);
  for (size_t i = 4800; i < burst.size(); ++i) burst[i].l = burst[i].r = 0.0f;
  float low[3], high[3];
  for (int k = 0; k < 3; ++k) {
    auto out = Run(burst, [&](size_t, Controls* c) {
      c->position = 0.5f;   // no shift
      c->density = 0.9f;
      c->bal = 0.5f * k;
    });
    Tilt(out, 48000, &low[k], &high[k]);
  }
  std::printf("    BAL left/noon/right: lows %.3f/%.3f/%.3f, highs %.3f/%.3f/%.3f\n",
              low[0], low[1], low[2], high[0], high[1], high[2]);
  CHECK(high[0] < high[1] * 0.1f, "BAL left should darken every pass");
  CHECK(low[2] < low[1] * 0.1f && high[2] > high[1], "BAL right should thin every pass");

  // FBK: wow. A 1 kHz sine through the loop (no feedback): the moving
  // delay spreads it around 1 kHz.
  auto sine = Sine(1000.0f, 3.0f, 0.3f);
  float on[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(sine, [&](size_t, Controls* c) {
      c->position = 0.5f;
      c->size = 0.5f;
      c->fbk = k ? 1.0f : 0.0f;
    });
    on[k] = ToneAmplitude(out, 1000.0f, 48000, out.size());
  }
  std::printf("    FBK: 1 kHz after the loop %.1f dB -> %.1f dB\n", Db(on[0]), Db(on[1]));
  CHECK(on[1] < on[0] * 0.5f, "FBK should detune the loop");

  // RVB: dispersion. A click recirculating through a 49 ms loop: every
  // pass delays the lows behind the highs, so in the third echo the
  // energy below 300 Hz peaks well after the energy above 3 kHz.
  auto click = Silence(1.0f);
  click[2400].l = click[2400].r = 0.9f;
  const float loop = 72.0f * std::pow(24000.0f / 72.0f, 0.6f);
  float lag[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(click, [&](size_t, Controls* c) {
      c->position = 0.5f;
      c->size = 0.6f;
      c->density = 0.7f;
      c->rvb = k ? 1.0f : 0.0f;
    });
    Stats s = Measure(out);
    CHECK(s.finite && s.peak <= 1.0f, "the loop must stay bounded");
    // Where each band's energy peaks (1 ms slots) in the third echo.
    Svf lp, hp;
    lp.Init();
    hp.Init();
    lp.set(300.0f / 48000.0f, 0.7f);
    hp.set(3000.0f / 48000.0f, 0.7f);
    const size_t from = 2400 + size_t(3.0f * loop) - 240;
    double el[45] = { 0 }, eh[45] = { 0 };
    for (size_t i = 0; i < from + 45 * 48; ++i) {
      float a = lp.Lp(out[i].l), b = hp.Hp(out[i].l);
      if (i < from) continue;
      el[(i - from) / 48] += a * a;
      eh[(i - from) / 48] += b * b;
    }
    int pl = 0, ph = 0;
    for (int w = 0; w < 45; ++w) {
      if (el[w] > el[pl]) pl = w;
      if (eh[w] > eh[ph]) ph = w;
    }
    lag[k] = float(pl - ph);
  }
  std::printf("    RVB: lows behind the highs in the third echo %.1f ms -> %.1f ms\n",
              lag[0], lag[1]);
  CHECK(lag[1] - lag[0] > 5.0f, "RVB should disperse the repeats");
  return true;
}
