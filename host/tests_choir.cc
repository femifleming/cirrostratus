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
