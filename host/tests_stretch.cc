// STRETCH behaviour tests.

#include <algorithm>
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
  const float hop = float(StretchMode::kNativeHop);
  CHECK(std::fabs(a0 - hop) < 1.0f, "1x at 0");
  CHECK(std::fabs(hop / a9 - 1000.0f) < 20.0f, "1000x at 0.9");
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
  // Random phases smear a partial over the window bandwidth (~+/-25 Hz),
  // so look at the strongest line near 440 Hz against lines well away.
  Module m;
  m.SetMode(MODE_STRETCH);
  auto in = Sine(440.0f, 3.0f, 0.5f);
  auto out = m.Render(in, [&](size_t) {
    Controls c = DefaultControls();
    c.size = 0.5f;
    c.texture = 1.0f;
    c.density = 0.0f;
    return c;
  });
  auto band_max = [&](float lo, float hi) {
    float best = 0.0f;
    for (float f = lo; f <= hi; f += 2.0f) {
      float v = ToneAmplitude(out, f, out.size() - 48000, out.size());
      if (v > best) best = v;
    }
    return best;
  };
  float on = band_max(410.0f, 470.0f);
  float off = std::max(band_max(250.0f, 350.0f), band_max(550.0f, 700.0f));
  std::printf("    random phases: near 440 Hz %.1f dB, elsewhere %.1f dB\n",
              Db(on), Db(off));
  CHECK(Db(on) > -30.0f, "partial kept");
  CHECK(on > off * 30.0f, "energy stays around the partial");
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
  // Buffer holds tone A (older) then tone B (newest), frozen and stretched.
  // After a trigger the newest tone must dominate. Run it both ways round
  // so a lucky read position can't pass the test.
  const float tones[2][2] = { { 220.0f, 660.0f }, { 660.0f, 220.0f } };
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_STRETCH, 1);
    auto in = Sine(tones[k][0], 1.0f, 0.5f);
    auto b2 = Sine(tones[k][1], 0.3f, 0.5f);
    auto b3 = Silence(3.0f);
    in.insert(in.end(), b2.begin(), b2.end());
    in.insert(in.end(), b3.begin(), b3.end());
    const size_t freeze_block = (48000 + 14400) / kBlockSize;
    const size_t trig_block = freeze_block + 1500;
    auto out = m.Render(in, [&](size_t b) {
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
    float newest = ToneAmplitude(out, tones[k][1], t + 9600, t + 33600);
    float older = ToneAmplitude(out, tones[k][0], t + 9600, t + 33600);
    std::printf("    after trigger: newest %.0f Hz %.1f dB, older %.0f Hz %.1f dB\n",
                tones[k][1], Db(newest), tones[k][0], Db(older));
    CHECK(newest > older * 4.0f, "trigger should jump to the newest audio");
  }
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

// Zero-crossing frequency of the left channel in 40 ms windows.
static std::vector<float> Track(const std::vector<FloatFrame>& v, size_t from,
                                size_t to) {
  std::vector<float> f;
  for (size_t i = from; i + 1920 <= to; i += 1920) {
    int z = 0;
    for (size_t j = i + 1; j < i + 1920; ++j) {
      if ((v[j - 1].l < 0.0f) != (v[j].l < 0.0f)) ++z;
    }
    f.push_back(z * 12.5f);  // crossings / 2 per 40 ms
  }
  return f;
}

MONSOON_TEST(stretch_bal_fbk_rvb_do_what_they_say) {
  // A sweep rising 300 -> 2400 Hz over 3 s, then FREEZE for 2 s: the
  // frozen buffer holds its last 0.3 s, read at 2x stretch.
  std::vector<FloatFrame> sweep(48000 * 5);
  double ph = 0;
  for (size_t i = 0; i < sweep.size(); ++i) {
    ph += 300.0 * std::pow(8.0, std::min(i, size_t(144000)) / 144000.0) / 48000.0;
    sweep[i].l = sweep[i].r = 0.4f * float(std::sin(2 * M_PI * ph));
  }
  auto run = [&](const std::vector<FloatFrame>& in, float bal, float fbk,
                 float rvb, float size, bool freeze) {
    Module m;
    m.SetMode(MODE_STRETCH);
    return m.Render(in, [&](size_t b) {
      Controls c = DefaultControls();
      c.size = size;
      c.position = 0.5f;
      c.density = 0.0f;
      c.texture = 0.0f;
      c.freeze = freeze && b * kBlockSize >= 144000;
      c.bal = bal; c.fbk = fbk; c.rvb = rvb;
      return c;
    });
  };
  // BAL left: forward the pitch rises (between wraps); backwards it falls.
  // BAL right: the read jumps about, so the pitch keeps changing direction.
  int rising[3], leaps[3];
  const float bals[3] = { 0.5f, 0.0f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    auto out = run(sweep, bals[k], 0.0f, 0.0f, 0.09f, true);
    auto f = Track(out, 153600, out.size());
    int up = 0, leap = 0, last = 0;
    for (size_t i = 2; i < f.size(); ++i) {
      if (f[i] > f[i - 2]) ++up;  // 80 ms apart
      // Changes of direction between neighbouring windows.
      const int dir = f[i] > f[i - 1] ? 1 : (f[i] < f[i - 1] ? -1 : 0);
      if (dir != 0) {
        if (last != 0 && dir != last) ++leap;
        last = dir;
      }
    }
    rising[k] = up * 100 / int(f.size() - 2);
    leaps[k] = leap;
  }
  std::printf("    BAL: windows with rising pitch: forward %d %%, backwards %d %%; "
              "pitch turns: %d (noon) vs %d (scatter)\n",
              rising[0], rising[1], leaps[0], leaps[2]);
  CHECK(rising[0] > 65 && rising[1] < 35, "BAL left does not reverse");
  CHECK(leaps[2] >= 2 * leaps[0] + 4, "BAL right does not scatter");

  // FBK: a burst, then silence: re-recording keeps it coming round.
  auto burst = Sine(500.0f, 2.0f, 0.4f);
  for (size_t i = 9600; i < burst.size(); ++i) burst[i].l = burst[i].r = 0.0f;
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    auto out = run(burst, 0.5f, k == 0 ? 0.0f : 1.0f, 0.0f, 0.0f, false);
    tail[k] = Measure(out, 48000, 96000).rms;  // 0.8-1.8 s after it
  }
  std::printf("    FBK: 1-2 s after a 0.2 s burst: %.1f dB -> %.1f dB\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(tail[1] > 30.0f * tail[0] && tail[1] > 0.01f, "FBK does not re-record");

  // RVB: the octave halo over a 440 Hz tone.
  auto tone = Sine(440.0f, 2.0f, 0.4f);
  float oct[2], fund[2];
  for (int k = 0; k < 2; ++k) {
    auto out = run(tone, 0.5f, 0.0f, k == 0 ? 0.0f : 1.0f, 0.0f, false);
    oct[k] = ToneAmplitude(out, 880.0f, 48000, 96000);
    fund[k] = ToneAmplitude(out, 440.0f, 48000, 96000);
  }
  std::printf("    RVB: 880 Hz %.1f -> %.1f dB (440 Hz %.1f -> %.1f dB)\n",
              Db(oct[0]), Db(oct[1]), Db(fund[0]), Db(fund[1]));
  CHECK(oct[1] > 0.3f * fund[1] && oct[1] > 100.0f * oct[0], "no octave halo");
  return true;
}
