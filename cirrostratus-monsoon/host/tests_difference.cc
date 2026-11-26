// DIFFERENCE mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

std::vector<FloatFrame> RenderDifference(const std::vector<FloatFrame>& in,
                                         Controls base,
                                         std::function<void(size_t, Controls*)> f = nullptr) {
  Module m;
  m.SetMode(MODE_DIFFERENCE);
  return m.Render(in, [&](size_t b) {
    Controls c = base;
    if (f) f(b, &c);
    return c;
  });
}

Controls Base() {
  Controls c = DefaultControls();
  c.position = 0.0f;  // f_a = 2 kHz
  c.size = 1.0f;      // whole input
  c.density = 0.0f;   // one pair
  c.texture = 0.0f;
  c.pitch = 0.0f;     // delta = 130.81 Hz
  return c;
}

}  // namespace

MONSOON_TEST(difference_pairs_land_at_fa_and_fa_plus_delta) {
  auto in = Sine(300.0f, 1.0f, 0.4f);
  auto out = RenderDifference(in, Base());
  size_t a = 24000, b = out.size();
  float lower = ToneAmplitude(out, 2300.0f, a, b);
  float upper = ToneAmplitude(out, 2430.81f, a, b);
  float mirror = ToneAmplitude(out, 1700.0f, a, b);   // wrong sideband
  float orig = ToneAmplitude(out, 300.0f, a, b);
  std::printf("    2300 Hz %.1f dB, 2430.8 Hz %.1f dB, mirror 1700 Hz %.1f dB, "
              "300 Hz %.1f dB\n", Db(lower), Db(upper), Db(mirror), Db(orig));
  CHECK(lower > 0.05f && upper > 0.05f, "pair tones missing");
  CHECK(Db(lower) - Db(mirror) > 35.0f, "sideband suppression %.1f dB",
        Db(lower) - Db(mirror));
  CHECK(Db(lower) - Db(orig) > 35.0f, "original leaks");
  return true;
}

MONSOON_TEST(difference_purity_makes_the_heard_tone_physical) {
  auto in = Sine(300.0f, 1.0f, 0.4f);
  Controls pure = Base();
  Controls physical = Base();
  physical.texture = 1.0f;
  auto o0 = RenderDifference(in, pure);
  auto o1 = RenderDifference(in, physical);
  float d0 = ToneAmplitude(o0, 130.81f, 24000, o0.size());
  float d1 = ToneAmplitude(o1, 130.81f, 24000, o1.size());
  std::printf("    tone at delta: texture 0 %.1f dB, texture 1 %.1f dB\n",
              Db(d0), Db(d1));
  CHECK(d1 > 0.02f, "no physical difference tone");
  CHECK(Db(d1) - Db(d0) > 30.0f, "texture 0 should keep it out of the signal");
  return true;
}

MONSOON_TEST(difference_pitch_sets_delta_volt_per_octave) {
  auto in = Sine(300.0f, 1.0f, 0.4f);
  Controls c = Base();
  c.pitch = 12.0f;
  c.texture = 1.0f;
  auto out = RenderDifference(in, c);
  float octave = ToneAmplitude(out, 261.63f, 24000, out.size());
  float old = ToneAmplitude(out, 130.81f, 24000, out.size());
  std::printf("    pitch +12: 261.6 Hz %.1f dB, 130.8 Hz %.1f dB\n", Db(octave), Db(old));
  CHECK(Db(octave) - Db(old) > 25.0f, "delta did not move an octave");
  return true;
}

MONSOON_TEST(difference_density_adds_pairs) {
  auto in = Sine(300.0f, 1.0f, 0.4f);
  Controls c = Base();
  c.density = 1.0f;
  auto out = RenderDifference(in, c);
  // Pair 2 sits at f_a + 1.5 * delta.
  float p2 = ToneAmplitude(out, 2300.0f + 1.5f * 130.81f, 24000, out.size());
  float p6 = ToneAmplitude(out, 2300.0f + 1.125f * 130.81f, 24000, out.size());
  std::printf("    pair 2 %.1f dB, pair 6 %.1f dB\n", Db(p2), Db(p6));
  CHECK(p2 > 0.02f && p6 > 0.02f, "extra pairs missing");
  return true;
}

MONSOON_TEST(difference_freeze_holds_a_drone) {
  std::vector<FloatFrame> in = Sine(300.0f, 1.0f, 0.4f);
  std::vector<FloatFrame> sil = Silence(2.0f);
  in.insert(in.end(), sil.begin(), sil.end());
  auto out = RenderDifference(in, Base(), [](size_t b, Controls* c) {
    c->freeze = b * kBlockSize > 36000;
  });
  // Input is silent from 1 s; the drone sits at f_a and f_a + delta.
  float fa = ToneAmplitude(out, 2000.0f, 96000, out.size());
  float fb = ToneAmplitude(out, 2130.81f, 96000, out.size());
  std::printf("    frozen drone: 2000 Hz %.1f dB, 2130.8 Hz %.1f dB\n", Db(fa), Db(fb));
  CHECK(fa > 0.03f && fb > 0.03f, "drone missing");
  return true;
}

MONSOON_TEST(difference_bal_fbk_rvb_do_what_they_say) {
  auto in = Sine(300.0f, 1.0f, 0.4f);
  // BAL left: glide. PITCH jumps an octave at 0.5 s; 50-250 ms later the
  // upper tone has reached 2300 + 261.6 Hz without glide, not with it.
  auto jump = [](float bal) {
    return [bal](size_t b, Controls* c) {
      c->bal = bal;
      c->pitch = b * kBlockSize >= 24000 ? 12.0f : 0.0f;
    };
  };
  auto snap = RenderDifference(in, Base(), jump(0.5f));
  auto glide = RenderDifference(in, Base(), jump(0.0f));
  float at_snap = ToneAmplitude(snap, 2561.6f, 26400, 36000);
  float at_glide = ToneAmplitude(glide, 2561.6f, 26400, 36000);
  std::printf("    BAL left: new note 50-250 ms after the jump: %.1f dB, gliding %.1f dB\n",
              Db(at_snap), Db(at_glide));
  CHECK(at_snap > 0.05f && at_glide < 0.2f * at_snap, "no glide");

  // BAL right: dichotic, f_a on the left ear, f_a + delta on the right.
  Controls d = Base();
  d.bal = 1.0f;
  auto dich = RenderDifference(in, d);
  size_t a = 24000, b = dich.size();
  float ll = ToneAmplitude(dich, 2300.0f, a, b, true);
  float lu = ToneAmplitude(dich, 2430.81f, a, b, true);
  float rl = ToneAmplitude(dich, 2300.0f, a, b, false);
  float ru = ToneAmplitude(dich, 2430.81f, a, b, false);
  std::printf("    BAL right: left ear 2300 %.1f / 2430.8 %.1f dB, right ear %.1f / %.1f dB\n",
              Db(ll), Db(lu), Db(rl), Db(ru));
  CHECK(ll > 0.05f && ru > 0.05f, "tones missing");
  CHECK(Db(ll) - Db(lu) > 30.0f && Db(ru) - Db(rl) > 30.0f, "not dichotic");

  // FBK: the spiral adds rungs a delta above: 2300 + 2 x 130.81 Hz.
  Controls f = Base();
  f.fbk = 1.0f;
  auto dry = RenderDifference(in, Base());
  auto spiral = RenderDifference(in, f);
  float rung_dry = ToneAmplitude(dry, 2561.6f, a, b);
  float rung = ToneAmplitude(spiral, 2561.6f, a, b);
  float base_dry = ToneAmplitude(dry, 2300.0f, a, b);
  float base = ToneAmplitude(spiral, 2300.0f, a, b);
  std::printf("    FBK: 2561.6 Hz rung %.1f -> %.1f dB; 2300 Hz %.1f -> %.1f dB\n",
              Db(rung_dry), Db(rung), Db(base_dry), Db(base));
  CHECK(rung > 0.03f && Db(rung) - Db(rung_dry) > 30.0f, "no spiral rung");
  CHECK(Measure(spiral, a, b).rms < 1.6f * Measure(dry, a, b).rms, "louder");

  // RVB: the engine's FDN.
  Module m;
  CHECK(m.SetMode(MODE_DIFFERENCE), "init");
  Controls r = Base();
  r.rvb = 0.7f;
  CHECK(m.engine().mode_instance(MODE_DIFFERENCE)->reverb_amount(r) == 0.7f,
        "RVB not mapped to the reverb");
  return true;
}
