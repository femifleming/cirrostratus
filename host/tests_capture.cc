// CAPTURE behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/capture.h"

using namespace host;
using namespace monsoon;

static CaptureMode* Capture(Module& m) {
  return static_cast<CaptureMode*>(m.engine().mode_instance(MODE_CAPTURE));
}

static std::vector<FloatFrame> Play(Module& m, float seconds, float pitch,
                                    float position, float texture) {
  auto in = Silence(seconds);
  return m.Render(in, [&](size_t) {
    Controls c = DefaultControls();
    c.pitch = pitch;
    c.position = position;
    c.texture = texture;
    c.density = 0.0f;   // single voice
    c.size = 0.5f;
    return c;
  });
}

MONSOON_TEST(capture_follows_volt_per_octave) {
  for (float pitch : {0.0f, 12.0f, 24.0f}) {
    Module m;
    m.SetMode(MODE_CAPTURE);
    auto out = Play(m, 1.0f, pitch, 0.0f, 0.5f);  // frame 0 = sine
    float f = 130.81f * std::pow(2.0f, pitch / 12.0f);
    float on = ToneAmplitude(out, f, 24000, 48000);
    float off = ToneAmplitude(out, f * 1.06f, 24000, 48000);
    std::printf("    pitch %+3.0f -> %.1f Hz: %.1f dB (semitone above %.1f dB)\n",
                pitch, f, Db(on), Db(off));
    CHECK(Db(on) > -20.0f, "note should sound");
    CHECK(on > off * 30.0f, "energy at the right note");
  }
  return true;
}

// Fraction of energy (dB) that is not near a harmonic of f.
static float InharmonicDb(const std::vector<FloatFrame>& v, float f) {
  double harm = 0, other = 0;
  for (float hz = 50.0f; hz < 23500.0f; hz += 37.0f) {
    float a = ToneAmplitude(v, hz, 0, 32768);
    float k = hz / f;
    bool near = std::fabs(k - std::round(k)) * f < 60.0f;
    (near ? harm : other) += double(a) * a;
  }
  return float(10 * std::log10(other / (harm + 1e-20) + 1e-20));
}

MONSOON_TEST(capture_high_notes_are_alias_free) {
  Module m;
  m.SetMode(MODE_CAPTURE);
  // Sawtooth frame, brightest texture, high note.
  auto out = Play(m, 1.0f, 36.0f, 1.0f, 1.0f);
  std::vector<FloatFrame> tail(out.end() - 32768, out.end());
  float db = InharmonicDb(tail, 1046.5f);
  std::printf("    saw at 1046.5 Hz: non-harmonic energy %.1f dB re harmonics\n", db);
  CHECK(db < -60.0f, "aliasing %.1f dB", db);
  return true;
}

MONSOON_TEST(capture_freeze_builds_table_from_input) {
  Module m;
  m.SetMode(MODE_CAPTURE);
  auto in = Sine(220.0f, 1.5f, 0.5f);
  m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.freeze = b > 2000;
    return c;
  });
  CaptureMode* cm = Capture(m);
  CHECK(!cm->building(), "build finished");
  float p = cm->detected_period(8);
  std::printf("    detected period %.2f samples (ideal %.2f)\n", p, 48000.0f / 220.0f);
  CHECK(std::fabs(p - 48000.0f / 220.0f) < 1.0f, "period");
  // Captured sine played at C3 should be a nearly pure tone.
  auto out = m.Render(Silence(1.0f), [](size_t) {
    Controls c = DefaultControls();
    c.freeze = true;
    c.position = 0.5f;
    c.density = 0.0f;
    c.texture = 1.0f;
    return c;
  });
  float h1 = ToneAmplitude(out, 130.81f, 24000, 48000);
  float h2 = ToneAmplitude(out, 261.63f, 24000, 48000);
  float h3 = ToneAmplitude(out, 392.44f, 24000, 48000);
  std::printf("    replay: H1 %.1f dB, H2 %.1f dB, H3 %.1f dB\n", Db(h1), Db(h2), Db(h3));
  CHECK(Db(h1) > -20.0f, "fundamental");
  CHECK(h2 < h1 * 0.03f && h3 < h1 * 0.03f, "captured sine stays pure");
  return true;
}

MONSOON_TEST(capture_gate_controls_the_envelope) {
  Module m;
  m.SetMode(MODE_CAPTURE);
  auto in = Silence(3.0f);
  auto out = m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.size = 0.2f;
    c.gate = b >= 1500 && b < 1600;   // one short gate at 1 s
    c.trigger = b == 1500;
    return c;
  });
  float drone = Measure(out, 24000, 48000).rms;
  float note = Measure(out, 48000 + 1000, 48000 + 4000).rms;
  float after = Measure(out, out.size() - 24000).rms;
  std::printf("    drone %.1f dB, note %.1f dB, after release %.1f dB\n",
              Db(drone), Db(note), Db(after));
  CHECK(Db(drone) > -30.0f, "drones before any gate");
  CHECK(Db(note) > -30.0f, "plays on the gate");
  CHECK(Db(after) < -70.0f, "silent after release");
  return true;
}

MONSOON_TEST(capture_morph_reads_the_slot) {
  // Slot: a table captured from a 220 Hz sine (all frames sine).
  Module a;
  a.SetMode(MODE_CAPTURE);
  a.Render(Sine(220.0f, 1.5f, 0.5f), [](size_t b) {
    Controls c = DefaultControls();
    c.freeze = b > 2000;
    return c;
  });
  PersistentChunk chunks[kMaxPersistentChunks + 1];
  size_t n = a.engine().GetPersistentData(chunks);
  std::vector<uint8_t> slot;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
    slot.insert(slot.end(), p, p + chunks[i].size);
    while (slot.size() & 3) slot.push_back(0);
  }
  Module b;
  b.SetMode(MODE_CAPTURE);
  // Position 1 without morph = the default sawtooth frame: bright.
  auto saw = Play(b, 0.5f, 0.0f, 1.0f, 1.0f);
  b.engine().SetMorphSource(slot.data(), slot.size());
  CHECK(Capture(b)->morph_active(), "morph armed");
  auto morphed = Play(b, 0.5f, 0.0f, 1.0f, 1.0f);
  float saw_h2 = ToneAmplitude(saw, 261.63f, 12000, 24000);
  float mor_h2 = ToneAmplitude(morphed, 261.63f, 12000, 24000);
  std::printf("    H2 at position 1: %.1f dB (own saw), %.1f dB (slot sine)\n",
              Db(saw_h2), Db(mor_h2));
  CHECK(mor_h2 < saw_h2 * 0.1f, "morph slot should change the timbre");
  return true;
}
