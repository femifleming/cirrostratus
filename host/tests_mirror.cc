// MIRROR mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/mirror.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Neutral() {
  Controls c = DefaultControls();
  c.position = 0.5f;
  c.size = 0.0f;
  c.density = 0.0f;
  c.texture = 0.5f;
  c.pitch = 0.0f;
  return c;
}

const float kBinHz = 48000.0f / 1024.0f;

}  // namespace

MONSOON_TEST(mirror_warp_function) {
  const float a = 261.63f / kBinHz;
  // Invert: an octave above the anchor lands an octave below.
  float inv = MirrorMode::Warp(2.0f * a, a, 0.5f, 1.0f);
  // Stretch: 2A -> A * 2^1.6.
  float st = MirrorMode::Warp(2.0f * a, a, 0.0f, 1.0f);
  // Fold: 1.5 A -> 0.5 A.
  float fo = MirrorMode::Warp(1.5f * a, a, 1.0f, 1.0f);
  float id = MirrorMode::Warp(3.0f * a, a, 0.3f, 0.0f);
  std::printf("    invert 2A -> %.3f A, stretch 2A -> %.3f A, fold 1.5A -> %.3f A, amount 0 -> %.3f A\n",
              inv / a, st / a, fo / a, id / a);
  CHECK(std::fabs(inv / a - 0.5f) < 0.01f, "invert");
  CHECK(std::fabs(st / a - 3.0314f) < 0.003f, "stretch");
  CHECK(std::fabs(fo / a - 0.5f) < 0.01f, "fold");
  CHECK(std::fabs(id / a - 3.0f) < 0.001f, "identity");
  return true;
}

MONSOON_TEST(mirror_amount_zero_passes_through) {
  Module m;
  CHECK(m.SetMode(MODE_MIRROR), "init");
  auto in = Sine(1000.0f, 2.0f, 0.3f);
  auto out = m.Render(in, [](size_t) { return Neutral(); });
  float a_in = ToneAmplitude(in, 1000.0f, 48000, 96000);
  float a_out = ToneAmplitude(out, 1000.0f, 48000, 96000);
  std::printf("    1 kHz in %.3f out %.3f (%.2f dB)\n", a_in, a_out, Db(a_out / a_in));
  CHECK(std::fabs(Db(a_out / a_in)) < 1.5f, "gain");
  return true;
}

MONSOON_TEST(mirror_inversion_moves_a_tone_to_its_mirror) {
  Module m;
  CHECK(m.SetMode(MODE_MIRROR), "init");
  // Anchor C4; input E5 (659.26 Hz, 19 semitones up) -> A2 (19 down).
  auto in = Sine(659.26f, 2.0f, 0.3f);
  auto out = m.Render(in, [](size_t) {
    Controls c = Neutral();
    c.size = 1.0f;
    c.position = 0.5f;
    return c;
  });
  const float mirror = 261.63f * 261.63f / 659.26f;  // 103.83 Hz
  float at_mirror = ToneAmplitude(out, mirror, 48000, 96000);
  float at_orig = ToneAmplitude(out, 659.26f, 48000, 96000);
  std::printf("    E5 in: %.1f Hz %.1f dB, original %.1f dB\n", mirror,
              Db(at_mirror), Db(at_orig));
  CHECK(at_mirror > 0.1f && at_mirror > 20.0f * at_orig, "not mirrored");
  return true;
}

MONSOON_TEST(mirror_pitch_moves_the_mirror_point) {
  Module m;
  CHECK(m.SetMode(MODE_MIRROR), "init");
  // Anchor up a fifth (G4 392 Hz): 659.26 Hz -> 392^2/659.26 = 233.1 Hz.
  auto in = Sine(659.26f, 2.0f, 0.3f);
  auto out = m.Render(in, [](size_t) {
    Controls c = Neutral();
    c.size = 1.0f;
    c.pitch = 7.0f;
    return c;
  });
  float target = 392.0f * 392.0f / 659.26f;
  float a = ToneAmplitude(out, target, 48000, 96000);
  std::printf("    anchor G4: tone at %.1f Hz %.1f dB\n", target, Db(a));
  CHECK(a > 0.1f, "mirror point did not follow PITCH");
  return true;
}

MONSOON_TEST(mirror_freeze_holds_the_spectrum) {
  Module m;
  CHECK(m.SetMode(MODE_MIRROR), "init");
  auto in = Sine(440.0f, 3.0f, 0.3f);
  for (size_t i = 48000; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
  auto out = m.Render(in, [](size_t b) {
    Controls c = Neutral();
    c.size = 1.0f;
    c.position = 0.0f;  // stretch
    c.freeze = b > 1300;
    return c;
  });
  float warped = 261.63f * std::pow(440.0f / 261.63f, 1.6f);
  float a = ToneAmplitude(out, warped, 96000, 144000);
  std::printf("    frozen, 1-2 s after the input stopped: %.1f Hz at %.1f dB\n",
              warped, Db(a));
  CHECK(a > 0.05f, "freeze lost the spectrum");
  return true;
}

// Per-mode knobs: BAL = linear frequency shift, FBK = warp feedback,
// RVB = the engine's FDN reverb.
MONSOON_TEST(mirror_bal_fbk_rvb_do_what_they_say) {
  auto run = [](const std::vector<FloatFrame>& in,
                std::function<void(size_t, Controls*)> f) {
    Module m;
    m.SetMode(MODE_MIRROR);
    return m.Render(in, [&](size_t b) {
      Controls c = Neutral();
      f(b, &c);
      return c;
    });
  };
  // BAL: a 1.5 kHz tone moves by -/+1 kHz.
  {
    auto in = Sine(1500.0f, 1.0f, 0.3f);
    auto down = run(in, [](size_t, Controls* c) { c->bal = 0.0f; });
    auto up = run(in, [](size_t, Controls* c) { c->bal = 1.0f; });
    const size_t a = 24000, b = 48000 - 64;
    float d500 = Db(ToneAmplitude(down, 500.0f, a, b));
    float d1500 = Db(ToneAmplitude(down, 1500.0f, a, b));
    float u2500 = Db(ToneAmplitude(up, 2500.0f, a, b));
    float u1500 = Db(ToneAmplitude(up, 1500.0f, a, b));
    std::printf("    BAL: 1.5 kHz in; left: 500 Hz %.1f dB (1.5 kHz %.1f dB); "
                "right: 2.5 kHz %.1f dB (1.5 kHz %.1f dB)\n", d500, d1500, u2500, u1500);
    CHECK(d500 > -14.0f && d1500 < d500 - 30.0f, "BAL left should shift down 1 kHz");
    CHECK(u2500 > -14.0f && u1500 < u2500 - 30.0f, "BAL right should shift up 1 kHz");
  }
  // FBK: a 0.3 s tone keeps going (and moving) after it stops; with a
  // small upward shift the tail glides up.
  {
    auto in = Sine(600.0f, 0.3f, 0.3f);
    auto s = Silence(1.7f);
    in.insert(in.end(), s.begin(), s.end());
    float tail[2];
    std::vector<FloatFrame> out[2];
    for (int k = 0; k < 2; ++k) {
      out[k] = run(in, [&](size_t, Controls* c) {
        c->fbk = k ? 1.0f : 0.0f;
        c->bal = 0.56f;   // a small upward shift
      });
      tail[k] = Db(Measure(out[k], 19200, 28800).rms);
      CHECK(Measure(out[k]).finite, "finite");
    }
    // Where is the tail? Strongest tone 0.1-0.3 s after the input stopped.
    float best = 0.0f, best_hz = 0.0f;
    for (float f = 300.0f; f < 4000.0f; f += 10.0f) {
      float v = ToneAmplitude(out[1], f, 19200, 28800);
      if (v > best) { best = v; best_hz = f; }
    }
    float late = Db(Measure(out[1], out[1].size() - 24000).rms);
    std::printf("    FBK: 0.1-0.3 s after the tone: off %.1f dB, full %.1f dB "
                "(strongest at %.0f Hz); last 0.5 s %.1f dB\n",
                tail[0], tail[1], best_hz, late);
    CHECK(tail[1] > tail[0] + 40.0f, "FBK should keep the spectrum going");
    CHECK(best_hz > 700.0f, "the fed-back partial should keep moving up");
    CHECK(late < tail[1] - 20.0f, "the spiral must decay");
  }
  // FBK on sustained music, no warp and full warp with bright tilt: the
  // level stays near the dry warp's.
  {
    auto in = Music(3.0f);
    float lv[4];
    for (int k = 0; k < 4; ++k) {
      auto out = run(in, [&](size_t, Controls* c) {
        c->fbk = (k & 1) ? 1.0f : 0.0f;
        if (k >= 2) { c->size = 1.0f; c->position = 0.0f; c->texture = 1.0f; }
      });
      lv[k] = Db(Measure(out, 48000).rms);
    }
    std::printf("    FBK on music: no warp %.1f -> %.1f dB, stretch + bright %.1f -> %.1f dB\n",
                lv[0], lv[1], lv[2], lv[3]);
    CHECK(lv[1] - lv[0] < 6.0f && lv[3] - lv[2] < 6.0f, "FBK level");
  }
  // RVB: the engine's FDN: a tail after the input stops (the mode alone
  // stops within a frame), and the mono output gains a stereo space.
  {
    auto in = Sine(1000.0f, 0.5f, 0.3f);
    auto s = Silence(1.5f);
    in.insert(in.end(), s.begin(), s.end());
    float tail[2], corr[2];
    for (int k = 0; k < 2; ++k) {
      auto out = run(in, [&](size_t, Controls* c) { c->rvb = k ? 1.0f : 0.0f; });
      tail[k] = Db(Measure(out, 48000, 72000).rms);
      double lr = 0, ll = 0, rr = 0;
      for (size_t i = 26400; i < 72000; ++i) {
        lr += out[i].l * out[i].r; ll += out[i].l * out[i].l; rr += out[i].r * out[i].r;
      }
      corr[k] = float(lr / std::sqrt(ll * rr + 1e-30));
    }
    std::printf("    RVB: 0.5-1 s after the tone: off %.1f dB, full %.1f dB; tail L/R "
                "correlation %.2f (the mode alone is mono)\n", tail[0], tail[1], corr[1]);
    CHECK(tail[1] > tail[0] + 40.0f && tail[1] > -60.0f, "RVB should add a tail");
    CHECK(std::fabs(corr[1]) < 0.7f, "RVB should add space");
  }
  // Random fast CV on all three plus SIZE: bounded.
  {
    Random r;
    r.Seed(11);
    auto out = run(Music(4.0f), [&](size_t, Controls* c) {
      c->size = r.Float(); c->texture = r.Float();
      c->bal = r.Float(); c->fbk = r.Float(); c->rvb = r.Float();
    });
    Stats st = Measure(out);
    std::printf("    random CV: peak %.2f, rms %.1f dB\n", st.peak, Db(st.rms));
    CHECK(st.finite && st.peak < 2.0f, "bounded");
  }
  return true;
}
