// PUTZ behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/putz.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Settings(float density) {
  Controls c = DefaultControls();
  c.position = 0.0f;   // hum comb off
  c.size = 0.5f;
  c.density = density;
  c.texture = 0.0f;
  c.pitch = 0.0f;
  return c;
}

// Hiss (white noise) with a tone that starts after `tone_from` seconds.
std::vector<FloatFrame> HissAndTone(float seconds, float tone_from) {
  auto v = Noise(seconds, 0.05f);
  for (size_t i = size_t(tone_from * 48000); i < v.size(); ++i) {
    float s = 0.3f * std::sin(2.0 * M_PI * 660.0 * i / 48000.0);
    v[i].l += s;
    v[i].r += s;
  }
  return v;
}

}  // namespace

MONSOON_TEST(putz_learned_noise_is_removed_and_the_tone_kept) {
  // 0-1.5 s: hiss only, FREEZE held (learn). 2-3 s: hiss only, cleaned.
  // 3-5 s: hiss + tone.
  auto in = HissAndTone(5.0f, 3.0f);
  float hiss[2], tone[2], noise_with_tone[2];
  for (int on = 0; on < 2; ++on) {
    Module m;
    m.SetMode(MODE_PUTZ);
    PutzMode* p = static_cast<PutzMode*>(m.engine().mode_instance(MODE_PUTZ));
    auto out = m.Render(in, [&](size_t b) {
      Controls c = Settings(on ? 1.0f : 0.0f);
      c.freeze = b > 100 && b < 2200;
      return c;
    });
    if (on) CHECK(p->learned(), "profile not learned");
    hiss[on] = Measure(out, 48000 * 2, 48000 * 3 - 32).rms;
    tone[on] = ToneAmplitude(out, 660.0f, 48000 * 4, 48000 * 5 - 32);
    noise_with_tone[on] = ToneAmplitude(out, 2300.0f, 48000 * 4, 48000 * 5 - 32);
  }
  std::printf("    hiss: %.1f dB -> %.1f dB; tone: %.1f dB -> %.1f dB\n",
              Db(hiss[0]), Db(hiss[1]), Db(tone[0]), Db(tone[1]));
  CHECK(Db(hiss[0]) - Db(hiss[1]) > 15.0f, "noise floor not lowered");
  CHECK(std::fabs(Db(tone[0]) - Db(tone[1])) < 2.0f, "tone damaged");
  (void)noise_with_tone;
  return true;
}

MONSOON_TEST(putz_tracks_noise_without_learning) {
  auto in = HissAndTone(6.0f, 100.0f);  // hiss only
  Module m;
  m.SetMode(MODE_PUTZ);
  auto out = m.Render(in, [](size_t) { return Settings(1.0f); });
  float early = Measure(in, 48000 * 5, 48000 * 6 - 32).rms;
  float late = Measure(out, 48000 * 5, 48000 * 6 - 32).rms;
  std::printf("    automatic noise tracking: hiss %.1f dB in, %.1f dB out after 5 s\n",
              Db(early), Db(late));
  CHECK(Db(early) - Db(late) > 10.0f, "minimum statistics not working");
  return true;
}

MONSOON_TEST(putz_hum_comb_removes_mains_harmonics) {
  const float bases[2] = { 50.0f, 60.0f };
  for (int option = 0; option < 2; ++option) {
    const float f0 = bases[option];
    std::vector<FloatFrame> in = Silence(3.0f);
    for (size_t i = 0; i < in.size(); ++i) {
      float s = 0.0f;
      for (int h = 1; h <= 5; ++h) {
        s += 0.08f * std::sin(2.0 * M_PI * f0 * h * i / 48000.0);
      }
      s += 0.1f * std::sin(2.0 * M_PI * 1234.0 * i / 48000.0);  // music
      in[i].l = in[i].r = s;
    }
    float hum[2], music[2];
    for (int on = 0; on < 2; ++on) {
      Module m;
      m.SetMode(MODE_PUTZ, option);
      auto out = m.Render(in, [&](size_t) {
        Controls c = Settings(0.0f);
        c.position = on ? 1.0f : 0.0f;
        c.option = option;
        return c;
      });
      hum[on] = 0.0f;
      for (int h = 1; h <= 5; ++h) {
        hum[on] += ToneAmplitude(out, f0 * h, 48000, 48000 * 3 - 32);
      }
      music[on] = ToneAmplitude(out, 1234.0f, 48000, 48000 * 3 - 32);
    }
    std::printf("    %.0f Hz hum: %.1f dB -> %.1f dB; 1234 Hz tone %.1f -> %.1f dB\n",
                f0, Db(hum[0]), Db(hum[1]), Db(music[0]), Db(music[1]));
    CHECK(Db(hum[0]) - Db(hum[1]) > 20.0f, "hum not removed");
    CHECK(std::fabs(Db(music[0]) - Db(music[1])) < 1.0f, "music damaged");
  }
  return true;
}

MONSOON_TEST(putz_listen_plays_the_removed_noise) {
  auto in = HissAndTone(4.0f, 2.0f);
  Module m;
  m.SetMode(MODE_PUTZ);
  auto out = m.Render(in, [](size_t b) {
    Controls c = Settings(1.0f);
    c.freeze = b > 100 && b < 2200;   // learn the hiss
    c.trigger = b == 2400;            // then listen
    return c;
  });
  float tone = ToneAmplitude(out, 660.0f, 48000 * 3, 48000 * 4 - 32);
  float hiss = Measure(out, 48000 * 3, 48000 * 4 - 32).rms;
  std::printf("    listen: removed signal %.1f dB rms, tone in it %.1f dB\n",
              Db(hiss), Db(tone));
  CHECK(Db(hiss) > -45.0f, "listen output should hold the hiss");
  CHECK(Db(tone) < -30.0f, "listen output should not hold the tone");
  return true;
}
