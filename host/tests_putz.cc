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

namespace {

// Energy of one side below `lo` Hz and above `hi` Hz (Svf low/high-pass).
void Bands(const std::vector<FloatFrame>& v, size_t from, float lo, float hi,
           float* low, float* high) {
  Svf lp, hp;
  lp.Init();
  hp.Init();
  lp.set(lo / 48000.0f, 0.7f);
  hp.set(hi / 48000.0f, 0.7f);
  double el = 0, eh = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    float a = lp.Lp(v[i].l), b = hp.Hp(v[i].l);
    if (i < from) continue;
    el += a * a;
    eh += b * b;
  }
  *low = float(el);
  *high = float(eh);
}

std::vector<FloatFrame> Clean(const std::vector<FloatFrame>& in, Controls c) {
  Module m;
  m.SetMode(MODE_PUTZ);
  return m.Render(in, [&](size_t) { return c; });
}

}  // namespace

MONSOON_TEST(putz_bal_fbk_rvb_do_what_they_say) {
  // BAL: tilt of the reduction. Hiss only, tracked automatically.
  auto hiss = Noise(4.0f, 0.1f);
  float low[3], high[3];
  for (int k = 0; k < 3; ++k) {
    Controls c = Settings(0.6f);
    c.bal = 0.5f * k;
    auto out = Clean(hiss, c);
    Bands(out, 48000 * 2, 400.0f, 5000.0f, &low[k], &high[k]);
  }
  std::printf("    BAL left/noon/right: lows %.1f/%.1f/%.1f dB, highs %.1f/%.1f/%.1f dB\n",
              10 * std::log10(low[0]), 10 * std::log10(low[1]), 10 * std::log10(low[2]),
              10 * std::log10(high[0]), 10 * std::log10(high[1]), 10 * std::log10(high[2]));
  CHECK(low[0] < low[1] * 0.7f && low[2] > low[1] * 1.4f, "BAL should move the low-band cleaning");
  CHECK(high[2] < high[1] * 0.7f && high[0] > high[1] * 1.4f, "BAL should move the high-band cleaning");

  // FBK: de-crackle. A 220 Hz tone with single-sample clicks every 0.1 s.
  auto ticks = Sine(220.0f, 3.0f, 0.3f);
  for (size_t i = 2400; i < ticks.size(); i += 4800) {
    float s = (i / 4800) & 1 ? 0.6f : -0.6f;
    ticks[i].l += s;
    ticks[i].r += s;
  }
  float click[2], tone[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Settings(0.0f);
    c.fbk = k ? 1.0f : 0.0f;
    auto out = Clean(ticks, c);
    float lo;
    Bands(out, 48000, 100.0f, 6000.0f, &lo, &click[k]);
    tone[k] = ToneAmplitude(out, 220.0f, 48000, out.size());
  }
  std::printf("    FBK: clicks above 6 kHz %.1f dB -> %.1f dB, tone %.2f dB -> %.2f dB\n",
              10 * std::log10(click[0]), 10 * std::log10(click[1]), Db(tone[0]), Db(tone[1]));
  CHECK(click[1] < click[0] * 0.1f, "FBK should flatten the clicks by > 10 dB");
  CHECK(std::fabs(Db(tone[1]) - Db(tone[0])) < 0.5f, "FBK should leave the tone alone");

  // RVB: de-reverb. Noise bursts (50 ms) with an exponential tail (RT60
  // 1.5 s), one per second.
  std::vector<FloatFrame> room = Silence(6.0f);
  Random r;
  r.Seed(3);
  for (size_t i = 0; i < room.size(); ++i) {
    size_t t = i % 48000;
    float env = t < 2400 ? 1.0f : std::pow(10.0f, -3.0f * (t - 2400) / (1.5f * 48000.0f));
    room[i].l = room[i].r = 0.3f * env * r.Bipolar();
  }
  float burst[2], tail[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Settings(0.0f);
    c.rvb = k ? 1.0f : 0.0f;
    auto out = Clean(room, c);
    burst[k] = tail[k] = 0.0f;
    for (int s = 2; s < 6; ++s) {
      size_t at = s * 48000 + 1536;   // the STFT latency
      burst[k] += Measure(out, at + 240, at + 2160).rms;
      tail[k] += Measure(out, at + 14400, at + 38400).rms;
    }
  }
  std::printf("    RVB: burst %.1f -> %.1f dB, tail (0.3-0.8 s) %.1f -> %.1f dB\n",
              Db(burst[0]), Db(burst[1]), Db(tail[0]), Db(tail[1]));
  CHECK(Db(tail[0]) - Db(tail[1]) > 10.0f, "RVB should pull the tail down by > 10 dB");
  CHECK(Db(burst[0]) - Db(burst[1]) < 3.0f, "RVB should keep the burst");
  return true;
}
