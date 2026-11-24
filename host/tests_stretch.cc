// STRETCH behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/stretch.h"

using namespace host;
using namespace monsoon;

static StretchMode* Stretch(Module& m) {
  return static_cast<StretchMode*>(m.engine().mode_instance(MODE_STRETCH));
}

MONSOON_TEST(stretch_factor_mapping) {
  float a0 = StretchMode::AdvancePerHop(0.0f);
  float a9 = StretchMode::AdvancePerHop(0.9f);
  float a1 = StretchMode::AdvancePerHop(1.0f);
  std::printf("    advance/hop: %.2f (size 0), %.4f (0.9), %.4f (1.0)\n", a0, a9, a1);
  CHECK(std::fabs(a0 - 512.0f) < 1.0f, "1x at 0");
  CHECK(std::fabs(512.0f / a9 - 1000.0f) < 20.0f, "1000x at 0.9");
  CHECK(a1 == 0.0f, "standstill at 1");
  return true;
}

static float Tone(float pitch, float texture, float hz) {
  Module m;
  m.SetMode(MODE_STRETCH);
  auto in = Sine(440.0f, 3.0f, 0.5f);
  auto out = m.Render(in, [&](size_t) {
    Controls c = DefaultControls();
    c.size = 0.0f;
    c.position = 0.0f;
    c.density = 0.0f;
    c.texture = texture;
    c.pitch = pitch;
    return c;
  });
  return ToneAmplitude(out, hz, out.size() - 48000, out.size());
}

MONSOON_TEST(stretch_realtime_keeps_pitch_and_level) {
  float a = Tone(0.0f, 0.0f, 440.0f);
  std::printf("    1x, coherent: 440 Hz at %.1f dB (input -6.0 dB)\n", Db(a));
  CHECK(Db(a) > -12.0f && Db(a) < -1.0f, "level %.1f", Db(a));
  return true;
}

MONSOON_TEST(stretch_pitch_shifts_without_changing_speed) {
  float up = Tone(12.0f, 0.0f, 880.0f);
  float orig = Tone(12.0f, 0.0f, 440.0f);
  std::printf("    +12 st: 880 Hz %.1f dB, 440 Hz %.1f dB\n", Db(up), Db(orig));
  CHECK(up > orig * 10.0f, "octave up should dominate");
  return true;
}

MONSOON_TEST(stretch_paulstretch_texture_still_tonal) {
  float a = Tone(0.0f, 1.0f, 440.0f);
  std::printf("    random phases: 440 Hz at %.1f dB\n", Db(a));
  CHECK(Db(a) > -24.0f, "random-phase stretch keeps the partial");
  return true;
}

MONSOON_TEST(stretch_infinite_holds_after_input_stops) {
  Module m;
  m.SetMode(MODE_STRETCH);
  auto in = Music(2.0f);
  auto tail = Silence(4.0f);
  in.insert(in.end(), tail.begin(), tail.end());
  auto out = m.Render(in, [&](size_t b) {
    Controls c = DefaultControls();
    c.size = 1.0f;
    c.texture = 1.0f;
    c.freeze = b > 2900;
    return c;
  });
  float rms = Measure(out, out.size() - 48000).rms;
  std::printf("    frozen + infinite stretch, last second: %.1f dB\n", Db(rms));
  CHECK(Db(rms) > -40.0f, "should keep sounding");
  return true;
}

MONSOON_TEST(stretch_mono_option_doubles_the_buffer) {
  Module m;
  m.SetMode(MODE_STRETCH, 0);
  float stereo = Stretch(m)->buffer_seconds();
  m.SetMode(MODE_STRETCH, 1);
  float mono = Stretch(m)->buffer_seconds();
  std::printf("    buffer: %.2f s stereo, %.2f s mono\n", stereo, mono);
  CHECK(mono > stereo * 1.9f, "mono should be ~2x");
  return true;
}

MONSOON_TEST(stretch_trigger_jumps_to_newest_audio) {
  // Buffer holds 220 Hz (older) then 660 Hz (newest), frozen. Reading the
  // old part, a trigger jumps to the newest material.
  Module m;
  m.SetMode(MODE_STRETCH, 1);
  auto in = Sine(220.0f, 1.0f, 0.5f);
  auto b2 = Sine(660.0f, 0.3f, 0.5f);
  auto b3 = Silence(3.0f);
  in.insert(in.end(), b2.begin(), b2.end());
  in.insert(in.end(), b3.begin(), b3.end());
  const size_t freeze_block = (48000 + 14400) / kBlockSize;
  const size_t trig_block = freeze_block + 1500;
  float before_hi = 0, before_lo = 0;
  std::vector<FloatFrame> out = m.Render(in, [&](size_t b) {
    Controls c = DefaultControls();
    c.size = 0.85f;
    c.position = 0.9f;
    c.texture = 0.0f;
    c.density = 0.0f;
    c.freeze = b >= freeze_block;
    c.trigger = b == trig_block;
    return c;
  });
  size_t t = trig_block * kBlockSize;
  before_hi = ToneAmplitude(out, 660.0f, t - 24000, t);
  before_lo = ToneAmplitude(out, 220.0f, t - 24000, t);
  float hi = ToneAmplitude(out, 660.0f, t + 9600, t + 33600);
  float lo = ToneAmplitude(out, 220.0f, t + 9600, t + 33600);
  std::printf("    before trigger: 220 Hz %.1f dB, 660 Hz %.1f dB\n",
              Db(before_lo), Db(before_hi));
  std::printf("    after trigger:  220 Hz %.1f dB, 660 Hz %.1f dB\n", Db(lo), Db(hi));
  CHECK(before_lo > before_hi * 4.0f, "reading the old part first");
  CHECK(hi > lo * 4.0f, "trigger should jump to the newest audio");
  return true;
}
MONSOON_TEST(stretch_keeps_pitch_and_level_at_every_factor) {
  for (float size : {0.0f, 0.5f, 0.7f, 0.85f, 1.0f}) {
    for (int fr = 0; fr < 2; ++fr) {
      Module m;
      m.SetMode(MODE_STRETCH, 1);
      auto in = Sine(220.0f, 3.0f, 0.5f);
      auto out = m.Render(in, [&](size_t b) {
        Controls c = DefaultControls();
        c.size = size;
        c.texture = 0.0f;
        c.density = 0.0f;
        c.freeze = fr && b >= 1950;
        return c;
      });
      float best = 0, bf = 0;
      for (float f = 150; f < 300; f += 1) {
        float a = ToneAmplitude(out, f, 96000, 144000);
        if (a > best) { best = a; bf = f; }
      }
      std::printf("    size %.2f %-8s peak %.0f Hz %.1f dB\n", size,
                  fr ? "frozen" : "live", bf, Db(best));
      CHECK(std::fabs(bf - 220.0f) <= 2.0f, "pitch drifted");
      CHECK(Db(best) > -9.0f, "level dropped");
    }
  }
  return true;
}
