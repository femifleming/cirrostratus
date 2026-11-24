// POLISH behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/polish.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Neutral() {
  Controls c = DefaultControls();
  c.position = 0.5f;  // flat tilt
  c.size = 0.5f;      // width 1
  c.density = 0.0f;   // no compression
  c.texture = 0.0f;   // no saturation
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Run(const std::vector<FloatFrame>& in,
                            std::function<Controls(size_t)> ctl) {
  Module m;
  m.engine().set_mono_bass(false);  // measure the mode, not the output section
  m.SetMode(MODE_POLISH);
  return m.Render(in, ctl);
}

}  // namespace

MONSOON_TEST(polish_bands_sum_flat_when_neutral) {
  for (float hz : { 60.0f, 200.0f, 1000.0f, 3000.0f, 9000.0f }) {
    auto in = Sine(hz, 1.0f, 0.3f);  // mono
    auto out = Run(in, [](size_t) { return Neutral(); });
    float a = ToneAmplitude(out, hz, 24000, 48000 - 32);
    float r = ToneAmplitude(in, hz, 24000, 48000 - 32);
    std::printf("    %5.0f Hz: %+.2f dB\n", hz, Db(a) - Db(r));
    CHECK(std::fabs(Db(a) - Db(r)) < 0.3f, "crossover not flat at %.0f Hz", hz);
  }
  return true;
}

MONSOON_TEST(polish_compresses_dynamic_range) {
  // 1 s loud, 1 s quiet (20 dB apart), repeated.
  std::vector<FloatFrame> in = Silence(6.0f);
  for (size_t i = 0; i < in.size(); ++i) {
    float amp = ((i / 48000) & 1) ? 0.05f : 0.5f;
    float s = amp * std::sin(2.0 * M_PI * 440.0 * i / 48000.0);
    in[i].l = in[i].r = s;
  }
  auto measure = [&](float density) {
    auto out = Run(in, [&](size_t) {
      Controls c = Neutral();
      c.density = density;
      return c;
    });
    float loud = Measure(out, 48000 * 4 + 12000, 48000 * 5 - 32).rms;
    float quiet = Measure(out, 48000 * 5 + 12000, 48000 * 6 - 32).rms;
    return Db(loud) - Db(quiet);
  };
  float off = measure(0.0f), on = measure(1.0f);
  std::printf("    loud/quiet difference: %.1f dB uncompressed, %.1f dB at "
              "density 1\n", off, on);
  CHECK(std::fabs(off - 20.0f) < 0.5f, "neutral should keep the dynamics");
  CHECK(on < 14.0f, "not compressing");
  return true;
}

MONSOON_TEST(polish_bass_goes_mono_and_width_scales_the_side) {
  // Pure side signal: 60 Hz and 2 kHz in opposite phase on L and R.
  std::vector<FloatFrame> in = Silence(1.0f);
  for (size_t i = 0; i < in.size(); ++i) {
    float s = 0.2f * std::sin(2.0 * M_PI * 60.0 * i / 48000.0) +
        0.2f * std::sin(2.0 * M_PI * 2000.0 * i / 48000.0);
    in[i].l = s;
    in[i].r = -s;
  }
  auto out = Run(in, [](size_t) { return Neutral(); });
  float bass = ToneAmplitude(out, 60.0f, 24000, 48000 - 32);
  float high = ToneAmplitude(out, 2000.0f, 24000, 48000 - 32);
  auto wide = Run(in, [](size_t) { Controls c = Neutral(); c.size = 1.0f; return c; });
  float high_wide = ToneAmplitude(wide, 2000.0f, 24000, 48000 - 32);
  auto mono = Run(in, [](size_t) { Controls c = Neutral(); c.size = 0.0f; return c; });
  float high_mono = ToneAmplitude(mono, 2000.0f, 24000, 48000 - 32);
  std::printf("    side 60 Hz %.1f dB, side 2 kHz %.1f dB; width x2 %.1f dB, "
              "mono %.1f dB\n", Db(bass), Db(high), Db(high_wide), Db(high_mono));
  CHECK(Db(high) - Db(bass) > 20.0f, "bass should be mono");
  CHECK(std::fabs(Db(high) - Db(0.2f)) < 0.5f, "side at 2 kHz should pass");
  CHECK(std::fabs(Db(high_wide) - Db(high) - 6.0f) < 0.7f, "width x2");
  CHECK(Db(high_mono) < -60.0f, "width 0 should be mono");
  return true;
}

MONSOON_TEST(polish_freeze_bypasses_and_saturation_adds_harmonics) {
  auto in = Sine(200.0f, 1.0f, 0.5f);
  auto bypass = Run(in, [](size_t) {
    Controls c = Neutral();
    c.density = 1.0f; c.texture = 1.0f; c.pitch = 24.0f; c.freeze = true;
    return c;
  });
  float diff = 0.0f;
  for (size_t i = 24000; i < 48000; ++i) {
    diff = std::fmax(diff, std::fabs(bypass[i].l - in[i].l));
  }
  auto sat = Run(in, [](size_t) { Controls c = Neutral(); c.texture = 1.0f; return c; });
  auto clean = Run(in, [](size_t) { return Neutral(); });
  float h2_sat = ToneAmplitude(sat, 400.0f, 24000, 48000 - 32);
  float h3_sat = ToneAmplitude(sat, 600.0f, 24000, 48000 - 32);
  float h3_clean = ToneAmplitude(clean, 600.0f, 24000, 48000 - 32);
  std::printf("    bypass max difference %.5f; saturation H2 %.1f dB, H3 %.1f dB "
              "(clean H3 %.1f dB)\n", diff, Db(h2_sat), Db(h3_sat), Db(h3_clean));
  // The engine's wet-path DC blocker shifts a 200 Hz tone by ~0.01 rad
  // (0.006 here); POLISH itself passes the input through untouched.
  CHECK(diff < 0.01f, "FREEZE should bypass");
  CHECK(Db(h2_sat) > -50.0f && Db(h3_sat) > -40.0f, "no saturation harmonics");
  CHECK(Db(h3_clean) < -80.0f, "clean path should be clean");
  return true;
}

MONSOON_TEST(polish_trig_rematches_loudness) {
  auto in = Sine(440.0f, 4.0f, 0.1f);
  auto out = Run(in, [](size_t b) {
    Controls c = Neutral();
    c.density = 1.0f;          // +makeup makes it louder
    c.trigger = b == 3000;     // at 2 s: match
    return c;
  });
  float before = Measure(out, 48000, 48000 * 2 - 32).rms;
  float after = Measure(out, 48000 * 3 + 24000, 48000 * 4 - 32).rms;
  float ref = Measure(in, 48000, 48000 * 2).rms;
  std::printf("    input %.1f dB, before TRIG %.1f dB, after TRIG %.1f dB\n",
              Db(ref), Db(before), Db(after));
  CHECK(std::fabs(Db(after) - Db(ref)) < 1.0f, "auto-gain should match the input");
  return true;
}
