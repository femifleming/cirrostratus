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
  m.engine().set_mono_bass(false, true);  // measure the mode, not the output section
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

// Per-mode knobs: BAL = which band the compressor leans on, FBK = parallel
// compression, RVB = air exciter.
MONSOON_TEST(polish_bal_fbk_rvb_do_what_they_say) {
  // BAL: mean gain reduction of the low and high bands on music.
  float lean[3];
  const float bals[3] = { 0.0f, 0.5f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    Module m;
    m.engine().set_mono_bass(false);
    m.SetMode(MODE_POLISH);
    PolishMode* p = static_cast<PolishMode*>(m.engine().mode_instance(MODE_POLISH));
    double lo = 0, hi = 0;
    m.Render(Music(4.0f), [&](size_t b) {
      if (b > 1500) { lo += p->gain_reduction_db(0); hi += p->gain_reduction_db(2); }
      Controls c = Neutral();
      c.density = 0.8f;
      c.bal = bals[k];
      return c;
    });
    lean[k] = float(lo - hi) / float(6000 - 1500);
  }
  std::printf("    BAL: low minus high band gain reduction: left %+.1f dB, noon %+.1f dB, right %+.1f dB\n",
              lean[0], lean[1], lean[2]);
  CHECK(lean[0] > lean[1] + 3.0f, "BAL left should lean on the lows");
  CHECK(lean[2] < lean[1] - 3.0f, "BAL right should lean on the highs");

  // FBK: quiet material comes up a lot, loud material hardly.
  float lift[2];
  const float amps[2] = { 0.01f, 0.7f };
  for (int k = 0; k < 2; ++k) {
    auto in = Sine(1000.0f, 1.0f, amps[k]);
    auto off = Run(in, [](size_t) { return Neutral(); });
    auto on = Run(in, [](size_t) { Controls c = Neutral(); c.fbk = 1.0f; return c; });
    lift[k] = Db(Measure(on, 24000).rms) - Db(Measure(off, 24000).rms);
  }
  std::printf("    FBK: level change at -40 dBFS %+.1f dB, at -3 dBFS %+.1f dB\n", lift[0], lift[1]);
  CHECK(lift[0] > 8.0f, "FBK should lift quiet material");
  CHECK(lift[1] < 2.5f, "FBK should leave peaks alone");

  // RVB: a 3.5 kHz tone gains a 7 kHz harmonic; a 1 kHz tone gets nothing.
  float h2[2], low_air;
  for (int k = 0; k < 2; ++k) {
    auto out = Run(Sine(3500.0f, 1.0f, 0.3f), [&](size_t) {
      Controls c = Neutral(); c.rvb = k ? 1.0f : 0.0f; return c;
    });
    h2[k] = Db(ToneAmplitude(out, 7000.0f, 24000, 48000 - 32));
  }
  {
    auto out = Run(Sine(1000.0f, 1.0f, 0.3f), [&](size_t) {
      Controls c = Neutral(); c.rvb = 1.0f; return c;
    });
    low_air = Db(ToneAmplitude(out, 2000.0f, 24000, 48000 - 32));
  }
  std::printf("    RVB: 7 kHz from a 3.5 kHz tone: off %.1f dB, full %.1f dB; "
              "2 kHz from a 1 kHz tone %.1f dB\n", h2[0], h2[1], low_air);
  CHECK(h2[1] > h2[0] + 40.0f && h2[1] > -40.0f, "RVB should add air harmonics");
  CHECK(low_air < -70.0f, "RVB should leave material without highs alone");

  // Random fast CV on all three with compression and saturation: bounded.
  {
    Random r;
    r.Seed(2);
    auto out = Run(Music(3.0f), [&](size_t) {
      Controls c = Neutral();
      c.density = 1.0f; c.texture = 0.5f;
      c.bal = r.Float(); c.fbk = r.Float(); c.rvb = r.Float();
      return c;
    });
    Stats st = Measure(out);
    std::printf("    random CV: peak %.2f\n", st.peak);
    CHECK(st.finite && st.peak < 2.0f, "bounded");
  }
  return true;
}
