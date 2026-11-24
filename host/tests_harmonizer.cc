// HARMONIZER mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/harmonizer.h"

using namespace host;
using namespace monsoon;

namespace {

// POSITION in the middle of chord-set zone z (8 zones).
float Zone(int z) { return (z + 0.5f) / 8.0f; }

}  // namespace

MONSOON_TEST(harmonizer_tracks_pitch) {
  for (float hz : { 110.0f, 220.0f, 440.0f, 700.0f }) {
    Module m;
    CHECK(m.SetMode(MODE_HARMONIZER), "init");
    auto in = Sine(hz, 0.5f, 0.3f);
    m.Render(in, [](size_t) { return DefaultControls(); });
    auto* h = static_cast<HarmonizerMode*>(m.engine().mode_instance(MODE_HARMONIZER));
    float cents = 1200.0f * std::log2(h->tracked_hz() / hz);
    std::printf("    %.0f Hz tracked as %.2f Hz (%+.1f cents)\n", hz,
                h->tracked_hz(), cents);
    CHECK(h->voiced() && std::fabs(cents) < 8.0f, "tracking");
  }
  return true;
}

MONSOON_TEST(harmonizer_voices_land_on_scale_degrees) {
  // Key A (PITCH +9). A4 in; set "3rd + 5th".
  // Major: C#5 + E5. Minor: C5 + E5.
  const float expected[2][2] = { { 554.37f, 659.26f }, { 523.25f, 659.26f } };
  for (int option = 0; option < 2; ++option) {
    Module m;
    CHECK(m.SetMode(MODE_HARMONIZER, option), "init");
    // A slightly sharp A (+30 cents): the voices must still be exact.
    const float in_hz = 440.0f * std::pow(2.0f, 30.0f / 1200.0f);
    auto in = Sine(in_hz, 2.0f, 0.3f);
    auto out = m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.pitch = 9.0f;
      c.position = Zone(2);
      c.density = 1.0f;
      c.size = 0.0f;
      c.texture = 0.0f;
      c.option = option;
      return c;
    });
    for (int v = 0; v < 2; ++v) {
      float f = expected[option][v];
      float on = ToneAmplitude(out, f, 48000, 96000);
      float flat = ToneAmplitude(out, f * std::pow(2.0f, -0.5f / 12.0f), 48000, 96000);
      float sharp = ToneAmplitude(out, f * std::pow(2.0f, 0.5f / 12.0f), 48000, 96000);
      std::printf("    %s voice %d: %.2f Hz %.1f dB (quarter-tone off: %.1f / %.1f dB)\n",
                  option ? "minor" : "major", v, f, Db(on), Db(flat), Db(sharp));
      CHECK(on > 0.05f && on > 3.0f * flat && on > 3.0f * sharp, "voice off its degree");
    }
  }
  return true;
}

MONSOON_TEST(harmonizer_target_note_table) {
  int count = 0;
  // C major, E4 (64) in, triad set: G4 (67) and B4 (71).
  float a = HarmonizerMode::TargetNote(64.0f, 0, 0, 2, 0, &count);
  float b = HarmonizerMode::TargetNote(64.0f, 0, 0, 2, 1, &count);
  // Octave set: up and down.
  float up = HarmonizerMode::TargetNote(64.0f, 0, 0, 3, 0, &count);
  float down = HarmonizerMode::TargetNote(64.0f, 0, 0, 3, 1, &count);
  // Pentatonic, octave set: still exactly an octave (5 steps).
  float pent = HarmonizerMode::TargetNote(62.0f, 0, 3, 3, 0, &count);
  std::printf("    E4 -> %.0f, %.0f; octaves %.0f, %.0f; pentatonic D4 octave %.0f\n",
              a, b, up, down, pent);
  CHECK(a == 67.0f && b == 71.0f && up == 76.0f && down == 52.0f && pent == 74.0f,
        "table");
  return true;
}

MONSOON_TEST(harmonizer_density_adds_voices) {
  float levels[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_HARMONIZER), "init");
    auto in = Sine(261.63f, 1.5f, 0.3f);
    auto out = m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.position = Zone(6);  // triad + octave
      c.density = k == 0 ? 0.0f : 1.0f;
      return c;
    });
    levels[k] = ToneAmplitude(out, 523.25f, 48000, 72000);  // the octave voice
  }
  std::printf("    octave voice: density 0 %.1f dB, density 1 %.1f dB\n",
              Db(levels[0]), Db(levels[1]));
  CHECK(levels[1] > 10.0f * levels[0], "density");
  return true;
}

MONSOON_TEST(harmonizer_freeze_latches_notes) {
  Module m;
  CHECK(m.SetMode(MODE_HARMONIZER), "init");
  // C4 for 1 s, then D4; freeze latched while C4 played.
  auto in = Sine(261.63f, 2.5f, 0.3f);
  for (size_t i = 48000; i < in.size(); ++i) {
    in[i].l = in[i].r = 0.3f * std::sin(2 * M_PI * 293.66 * i / 48000.0);
  }
  auto out = m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.position = Zone(1);  // 5th up
    c.density = 0.0f;
    c.size = 0.0f;         // no humanise detune
    c.freeze = b > 1200;
    return c;
  });
  // Latched G4 (392) stays although the input moved to D4 (5th would be A4).
  float g = ToneAmplitude(out, 392.0f, 72000, 120000);
  float a = ToneAmplitude(out, 440.0f, 72000, 120000);
  std::printf("    latched G4 %.1f dB, unlatched A4 %.1f dB\n", Db(g), Db(a));
  CHECK(g > 0.05f && g > 5.0f * a, "latch");
  return true;
}
