// CHOIR behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/choir.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.rvb = 0.0f;
  c.size = 0.0f;
  c.texture = 1.0f;   // internal source
  c.density = 0.0f;   // unison
  c.position = 0.0f;  // "a"
  return c;
}

float PeakNear(const std::vector<FloatFrame>& v, float hz, float cents,
               size_t from, size_t to) {
  float best = 0.0f, best_hz = hz;
  for (float c = -cents; c <= cents; c += 1.0f) {
    float f = hz * std::pow(2.0f, c / 1200.0f);
    float a = ToneAmplitude(v, f, from, to) + ToneAmplitude(v, f, from, to, false);
    if (a > best) { best = a; best_hz = f; }
  }
  return best_hz;
}

std::vector<FloatFrame> Sing(Controls base, float seconds, bool freeze = true,
                             const std::vector<FloatFrame>* input = NULL,
                             Module* keep = NULL, int trig_block = -1) {
  Module local;
  Module& m = keep ? *keep : local;
  m.SetMode(MODE_CHOIR);
  std::vector<FloatFrame> in = input ? *input : Silence(seconds);
  return m.Render(in, [&](size_t b) {
    Controls c = base;
    c.freeze = freeze;
    c.trigger = int(b) == trig_block;
    return c;
  });
}

ChoirMode* Choir(Module& m) {
  return static_cast<ChoirMode*>(m.engine().mode_instance(MODE_CHOIR));
}

}  // namespace

MONSOON_TEST(choir_sings_the_chord) {
  // Major chord on C3: C3, E3, G3, C4 must all be present.
  Controls c = Base();
  c.density = 2.5f / 6.0f;
  c.position = 0.75f;   // "o": strong low formants
  auto out = Sing(c, 2.0f);
  const float notes[] = { 0, 4, 7, 12 };
  for (float n : notes) {
    float f = 130.81f * std::pow(2.0f, n / 12.0f);
    float found = PeakNear(out, f, 30.0f, 24000, 48000 * 2 - 64);
    float on = ToneAmplitude(out, f, 24000, 96000 - 64);
    float off = ToneAmplitude(out, f * std::pow(2.0f, 1.5f / 12.0f), 24000, 96000 - 64);
    std::printf("    note +%2.0f: %.2f Hz (expected %.2f), %.1f dB above off-note\n",
                n, found, f, Db(on) - Db(off));
    CHECK(std::fabs(1200.0f * std::log2(found / f)) < 6.0f, "note %+.0f out of tune", n);
    CHECK(on > off * 4.0f, "note %+.0f missing", n);
  }
  return true;
}

MONSOON_TEST(choir_trig_steps_inversions) {
  Module m;
  Controls c = Base();
  c.density = 2.5f / 6.0f;
  Sing(c, 0.2f, true, NULL, &m, 2);
  CHECK(Choir(m)->inversion() == 1, "one trigger -> first inversion");
  std::printf("    1st inversion voices: %.1f %.1f %.1f %.1f Hz\n", Choir(m)->voice_hz(0),
              Choir(m)->voice_hz(1), Choir(m)->voice_hz(2), Choir(m)->voice_hz(3));
  CHECK(std::fabs(Choir(m)->voice_hz(0) - 261.63f) < 0.5f, "root raised an octave");
  return true;
}

MONSOON_TEST(choir_vowels_move_formants) {
  // With a C3 source, harmonic 6 (785 Hz) sits on "a"'s F1 (800 Hz) but
  // far above "i"'s F1 (350 Hz), where harmonic 3 (392 Hz) is closer.
  float ratio[2];
  const float pos[2] = { 0.0f, 0.5f };   // a, i
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.position = pos[k];
    auto out = Sing(c, 1.5f);
    ratio[k] = ToneAmplitude(out, 6 * 130.81f, 24000, 72000 - 64) /
        ToneAmplitude(out, 3 * 130.81f, 24000, 72000 - 64);
  }
  std::printf("    785 Hz vs 392 Hz: %.1f dB on 'a', %.1f dB on 'i'\n",
              Db(ratio[0]), Db(ratio[1]));
  CHECK(ratio[0] > ratio[1] * 4.0f, "vowel should move the formants");
  return true;
}

MONSOON_TEST(choir_input_through_combs) {
  Controls c = Base();
  c.texture = 0.0f;
  auto music = Music(2.0f);
  auto sung = Sing(c, 2.0f, false, &music);
  auto quiet = Sing(c, 1.0f, false);
  Stats s = Measure(sung, 24000, sung.size());
  Stats q = Measure(quiet);
  std::printf("    music in: %.1f dBFS; silence in: %.1f dBFS\n", Db(s.rms), Db(q.rms));
  CHECK(Db(s.rms) > -45.0f, "input should sing");
  CHECK(Db(q.rms) < -80.0f, "silence without freeze");
  return true;
}

MONSOON_TEST(choir_freeze_sustains_without_input) {
  auto out = Sing(Base(), 4.0f);
  float a = Measure(out, 48000, 96000).rms;
  float b = Measure(out, 48000 * 3, 48000 * 4 - 64).rms;
  std::printf("    frozen choir: %.1f dB at 1-2 s, %.1f dB at 3-4 s\n", Db(a), Db(b));
  CHECK(Db(a) > -40.0f, "audible");
  CHECK(std::fabs(Db(a) - Db(b)) < 2.0f, "steady");
  return true;
}

MONSOON_TEST(choir_bal_fbk_rvb_do_what_they_say) {
  // BAL: the formants move without the notes. Share of the frozen
  // choir's energy above 1.5 kHz, and the root's pitch.
  float bright[3], root[3];
  for (int k = 0; k < 3; ++k) {
    Controls c = Base();
    c.bal = 0.5f * k;
    auto out = Sing(c, 1.5f);
    Svf hp;
    hp.Init();
    hp.set(1500.0f / 48000.0f, 0.7f);
    double e = 0, h = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      float x = hp.Hp(out[i].l);
      if (i < 24000) continue;
      e += out[i].l * out[i].l;
      h += x * x;
    }
    bright[k] = float(h / (e + 1e-30));
    root[k] = PeakNear(out, 130.81f, 30.0f, 24000, out.size() - 64);
  }
  std::printf("    BAL left/noon/right: energy above 1.5 kHz %.3f / %.3f / %.3f, "
              "root %.1f / %.1f / %.1f Hz\n",
              bright[0], bright[1], bright[2], root[0], root[1], root[2]);
  CHECK(bright[0] < bright[1] * 0.7f && bright[2] > bright[1] * 1.4f,
        "BAL should move the formants");
  CHECK(std::fabs(root[0] - root[2]) < 1.0f, "BAL must not move the notes");

  // FBK: the input's combs ring longer. A 0.1 s noise burst, TEXTURE 0.
  float ring[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.texture = 0.0f;
    c.fbk = k ? 1.0f : 0.0f;
    auto in = Silence(2.0f);
    Random r;
    r.Seed(5);
    for (size_t i = 4800; i < 9600; ++i) in[i].l = in[i].r = 0.3f * r.Bipolar();
    auto out = Sing(c, 2.0f, false, &in);
    Stats s = Measure(out);
    CHECK(s.finite && s.peak < 1.0f, "bounded");
    ring[k] = Measure(out, 9600 + 24000, 9600 + 48000).rms /
        (Measure(out, 4800, 9600).rms + 1e-20f);
  }
  std::printf("    FBK: 0.5-1 s after the burst %.1f dB -> %.1f dB (re the burst)\n",
              Db(ring[0]), Db(ring[1]));
  CHECK(Db(ring[1]) - Db(ring[0]) > 30.0f, "FBK should make the combs ring on");

  // RVB: the FDN service gives a tail after the input stops.
  float late[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.rvb = k ? 0.8f : 0.0f;
    auto in = Music(1.0f);
    in.resize(48000 * 3);
    auto out = Sing(c, 3.0f, false, &in);
    late[k] = Measure(out, 48000 * 2, 48000 * 3).rms;
  }
  std::printf("    RVB: 1-2 s after the input stops %.1f dB -> %.1f dB\n",
              Db(late[0]), Db(late[1]));
  CHECK(Db(late[1]) - Db(late[0]) > 30.0f && Db(late[1]) > -70.0f, "RVB should add a tail");
  return true;
}
