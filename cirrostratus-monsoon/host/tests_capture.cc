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

MONSOON_TEST(capture_bal_fbk_rvb_do_what_they_say) {
  // The default table's frame 0 is a pure sine, so every harmonic in the
  // output comes from BAL / FBK.
  const float f0 = 130.81f;
  auto play = [&](float bal, float fbk, float rvb, bool note) {
    Module m;
    m.SetMode(MODE_CAPTURE);
    auto in = Silence(2.0f);
    return m.Render(in, [&](size_t b) {
      Controls c = DefaultControls();
      c.position = 0.0f;  // sine frame
      c.texture = 1.0f;   // brightest mip level
      c.density = 0.0f;   // single voice
      c.size = 0.0f;      // short pluck
      c.bal = bal;
      c.fbk = fbk;
      c.rvb = rvb;
      if (note) {
        c.gate = b >= 300 && b < 600;
        c.trigger = b == 300;
      }
      return c;
    });
  };
  auto rel = [&](const std::vector<FloatFrame>& v, int h) {
    return Db(ToneAmplitude(v, h * f0, 24000, 72000)) -
        Db(ToneAmplitude(v, f0, 24000, 72000));
  };
  auto neutral = play(0.5f, 0.0f, 0.0f, false);
  auto bend = play(0.0f, 0.0f, 0.0f, false);
  auto sync = play(1.0f, 0.0f, 0.0f, false);
  auto fm = play(0.5f, 1.0f, 0.0f, false);
  std::printf("    2nd harmonic re f0: neutral %.1f, BAL left %.1f, FBK %.1f dB\n",
              rel(neutral, 2), rel(bend, 2), rel(fm, 2));
  std::printf("    6th harmonic re f0: neutral %.1f, BAL right %.1f dB\n",
              rel(neutral, 6), rel(sync, 6));
  // RVB = FDN: a tail after the note's release.
  auto dry = play(0.5f, 0.0f, 0.0f, true);
  auto wet = play(0.5f, 0.0f, 0.8f, true);
  const size_t tail0 = 600 * 32 + 9600, tail1 = tail0 + 24000;
  float t_dry = Measure(dry, tail0, tail1).rms, t_wet = Measure(wet, tail0, tail1).rms;
  std::printf("    0.2-0.7 s after release: dry %.1f dB, RVB %.1f dB\n", Db(t_dry), Db(t_wet));
  for (auto* v : { &bend, &sync, &fm, &wet }) {
    Stats s = Measure(*v);
    CHECK(s.finite && s.peak < 1.0f, "bounded");
  }
  CHECK(rel(neutral, 2) < -50.0f && rel(neutral, 6) < -50.0f, "neutral is a sine");
  CHECK(rel(bend, 2) > -20.0f, "BAL left should bend the sine into harmonics");
  CHECK(rel(sync, 6) > -20.0f, "BAL right should put a formant near 6 x f0");
  CHECK(rel(fm, 2) > -25.0f, "FBK should add harmonics (self-FM)");
  CHECK(Db(t_wet) - Db(t_dry) > 30.0f, "RVB should leave a reverb tail");
  return true;
}
