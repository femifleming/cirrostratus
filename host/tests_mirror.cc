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
