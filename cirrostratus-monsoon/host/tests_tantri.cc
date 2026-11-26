// TANTRI behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/tantri.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.rvb = 0.0f;
  c.fbk = 0.0f;
  c.density = 0.0f;   // tanpura off unless a test wants it
  c.texture = 0.0f;
  c.size = 0.0f;
  return c;
}

float PeakNear(const std::vector<FloatFrame>& v, float hz, float cents,
               size_t from, size_t to) {
  float best = 0.0f, best_hz = hz;
  for (float c = -cents; c <= cents; c += 1.0f) {
    float f = hz * std::pow(2.0f, c / 1200.0f);
    float a = ToneAmplitude(v, f, from, to) + ToneAmplitude(v, f, from, to, false);
    if (a > best) { best = a; best_hz = f; }
  }
  return best_hz;
}

std::vector<FloatFrame> Render(Controls base, float seconds, int trig_block = -1,
                               int freeze_block = -1) {
  Module m;
  m.SetMode(MODE_TANTRI);
  auto in = Silence(seconds);
  return m.Render(in, [&](size_t b) {
    Controls c = base;
    c.trigger = int(b) == trig_block;
    c.freeze = freeze_block >= 0 && int(b) >= freeze_block;
    return c;
  });
}

// Share of the energy above 2 kHz.
float Brightness(const std::vector<FloatFrame>& v, size_t from, size_t to) {
  Svf hp;
  hp.Init();
  hp.set(2000.0f / 48000.0f, 0.7f);
  double e = 0, h = 0;
  for (size_t i = 0; i < to; ++i) {
    float x = hp.Hp(v[i].l);
    if (i < from) continue;
    e += v[i].l * v[i].l;
    h += x * x;
  }
  return float(h / (e + 1e-20));
}

float StrokeAt(float position) { return (position + 0.5f) / 8.0f; }

}  // namespace

MONSOON_TEST(tantri_tanpura_sa_is_in_tune) {
  const float pitches[] = { 0.0f, 5.0f };
  for (float p : pitches) {
    for (int jaw = 0; jaw < 2; ++jaw) {
      Controls c = Base();
      c.pitch = p;
      c.density = 0.6f;
      c.texture = jaw ? 0.8f : 0.0f;
      auto out = Render(c, 4.0f);
      float sa = 130.81f * std::pow(2.0f, p / 12.0f);
      float found = PeakNear(out, sa, 40.0f, 48000, 48000 * 4 - 64);
      float cents = 1200.0f * std::log2(found / sa);
      std::printf("    pitch %+3.0f jawari %d: Sa %.2f Hz, measured %.2f (%+.1f cents)\n",
                  p, jaw, sa, found, cents);
      CHECK(std::fabs(cents) <= (jaw ? 10.0f : 4.0f), "Sa out of tune");
    }
  }
  return true;
}

MONSOON_TEST(tantri_every_stroke_sounds) {
  const char* names[] = { "Na", "Tin", "Tun", "Te", "Ge", "Ke", "Dha", "Dhin" };
  for (int s = 0; s < TantriMode::NUM_STROKES; ++s) {
    Controls c = Base();
    c.position = StrokeAt(s);
    auto out = Render(c, 0.6f, 4);
    Stats st = Measure(out, 0, 9600);
    std::printf("    %-4s %.1f dBFS\n", names[s], Db(st.rms));
    CHECK(Db(st.rms) > -40.0f, "%s too quiet", names[s]);
    CHECK(st.finite && st.peak <= 1.0f, "%s bounded", names[s]);
  }
  return true;
}

MONSOON_TEST(tantri_closed_strokes_are_damped) {
  float tail[2];
  const int strokes[2] = { TantriMode::TUN, TantriMode::TE };
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.position = StrokeAt(strokes[k]);
    auto out = Render(c, 1.0f, 4);
    tail[k] = Measure(out, 14400, 28800).rms / Measure(out, 0, 4800).rms;
  }
  std::printf("    tail at 0.3-0.6 s: Tun %.1f dB, Te %.1f dB\n", Db(tail[0]), Db(tail[1]));
  CHECK(tail[0] > tail[1] * 4.0f, "Te should be choked compared to Tun");
  return true;
}

MONSOON_TEST(tantri_tun_rings_at_dayan_pitch) {
  Controls c = Base();
  c.position = StrokeAt(TantriMode::TUN);
  auto out = Render(c, 1.0f, 4);
  float dayan = 2.0f * 130.81f;
  float found = PeakNear(out, dayan, 60.0f, 2400, 48000);
  float cents = 1200.0f * std::log2(found / dayan);
  std::printf("    Tun fundamental %.1f Hz (dayan %.1f, %+.1f cents)\n", found, dayan, cents);
  CHECK(std::fabs(cents) < 8.0f, "dayan detuned");
  return true;
}

MONSOON_TEST(tantri_size_bends_the_bayan) {
  float found[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_TANTRI);
    Controls base = Base();
    base.position = StrokeAt(TantriMode::GE);
    base.size = k == 0 ? 0.0f : 1.0f;
    auto in = Silence(2.0f);
    auto out = m.Render(in, [&](size_t b) {
      Controls c = base;
      c.trigger = b == 4;
      return c;
    });
    auto* t = static_cast<TantriMode*>(m.engine().mode_instance(MODE_TANTRI));
    float expect = 0.75f * 130.81f * (k == 0 ? 1.0f : 1.55f);
    // After the ghe glide has settled (~1 s).
    found[k] = PeakNear(out, expect, 60.0f, 48000 + 4800, 48000 * 2 - 64);
    std::printf("    size %d: bayan expected %.1f Hz, model %.1f Hz, heard %.1f Hz\n",
                k, expect, t->bayan_hz(), found[k]);
    CHECK(std::fabs(1200.0f * std::log2(t->bayan_hz() / expect)) < 3.0f, "model pitch");
    CHECK(std::fabs(1200.0f * std::log2(found[k] / expect)) < 15.0f, "heard pitch");
  }
  return true;
}

MONSOON_TEST(tantri_jawari_brightens_tanpura) {
  float b[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = 0.6f;
    c.texture = k ? 1.0f : 0.0f;
    auto out = Render(c, 3.0f);
    b[k] = Brightness(out, 48000, 48000 * 3);
  }
  std::printf("    energy above 2 kHz: %.4f plain, %.4f jawari\n", b[0], b[1]);
  CHECK(b[1] > b[0] * 2.0f, "jawari should add bright harmonics");
  return true;
}

MONSOON_TEST(tantri_freeze_holds_the_drone) {
  Controls c = Base();
  c.density = 0.7f;
  c.texture = 0.6f;
  auto out = Render(c, 9.0f, -1, 48000 * 3 / 32);
  float at3 = Measure(out, 48000 * 3, 48000 * 4).rms;
  float at8 = Measure(out, 48000 * 8, 48000 * 9 - 64).rms;
  std::printf("    frozen drone: %.1f dB at 3 s, %.1f dB at 8 s\n", Db(at3), Db(at8));
  CHECK(at8 > at3 * 0.35f, "drone should sustain");
  CHECK(at8 < at3 * 1.5f, "and not grow");
  return true;
}

MONSOON_TEST(tantri_input_excites_and_silence_is_silent) {
  Module m;
  m.SetMode(MODE_TANTRI);
  auto sil = Silence(1.0f);
  auto quiet = m.Render(sil, [](size_t) { return Base(); });
  auto in = Music(2.0f);
  auto played = m.Render(in, [](size_t) { return Base(); });
  float q = Measure(quiet).rms, p = Measure(played, 24000, played.size()).rms;
  std::printf("    silence in: %.1f dBFS, music in: %.1f dBFS\n", Db(q), Db(p));
  CHECK(Db(q) < -80.0f, "should be silent with nothing going on");
  CHECK(Db(p) > -45.0f, "input should excite strings and drums");
  return true;
}

MONSOON_TEST(tantri_bal_fbk_rvb_do_what_they_say) {
  // BAL: the first string's tuning, and it is really heard there.
  const float sa = 130.81f;
  const float want[3] = { 2.0f / 3.0f, 0.75f, 15.0f / 16.0f };
  const float bals[3] = { 0.0f, 0.5f, 1.0f };
  const char* names[3] = { "Ma", "Pa", "Ni" };
  for (int k = 0; k < 3; ++k) {
    Module m;
    m.SetMode(MODE_TANTRI);
    Controls c = Base();
    c.density = 0.8f;
    c.bal = bals[k];
    auto out = m.Render(Silence(4.0f), [&](size_t) { return c; });
    auto* t = static_cast<TantriMode*>(m.engine().mode_instance(MODE_TANTRI));
    float ratio = t->string_hz(0) / sa;
    float found = PeakNear(out, sa * want[k], 30.0f, 48000, 48000 * 4 - 64);
    float cents = 1200.0f * std::log2(found / (sa * want[k]));
    std::printf("    BAL %.1f: first string %s %.2f Hz (x %.4f), heard at %+.1f cents\n",
                bals[k], names[k], t->string_hz(0), ratio, cents);
    CHECK(std::fabs(1200.0f * std::log2(ratio / want[k])) < 3.0f, "tuning");
    CHECK(std::fabs(cents) < 10.0f, "heard");
  }

  // FBK: a closed Na rings on instead of being damped.
  float ring[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.position = StrokeAt(0);  // Na
    c.fbk = k ? 1.0f : 0.0f;
    auto out = Render(c, 2.0f, 10);
    ring[k] = Measure(out, 24000, 48000).rms;
  }
  std::printf("    FBK Na, 0.5-1 s after: %.1f dB -> %.1f dB\n", Db(ring[0]), Db(ring[1]));
  CHECK(ring[1] > 4.0f * ring[0], "ring");

  // FBK also lets the drone set the drum heads humming: energy at the
  // dayan's pitch (upper Sa) with the drone alone.
  float hum[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = 0.6f;
    c.fbk = k ? 1.0f : 0.0f;
    auto out = Render(c, 4.0f);
    hum[k] = ToneAmplitude(out, 2.0f * sa, 96000, 192000 - 64);
  }
  std::printf("    FBK drone hum at upper Sa: %.1f dB -> %.1f dB\n", Db(hum[0]), Db(hum[1]));
  CHECK(hum[1] > 1.4f * hum[0], "sympathetic drums");

  // RVB: the engine's FDN: a room tail after a Tin.
  float tail[2];
  float peak = 0.0f;
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.position = StrokeAt(1);  // Tin
    c.rvb = k ? 1.0f : 0.0f;
    auto out = Render(c, 5.0f, 10);
    tail[k] = Measure(out, 48000 * 3, 48000 * 4).rms;
    if (k) peak = Measure(out).peak;
  }
  std::printf("    RVB 3-4 s after a Tin: %.1f dB -> %.1f dB (peak %.2f)\n",
              Db(tail[0]), Db(tail[1]), peak);
  CHECK(tail[1] > 10.0f * tail[0] && peak < 0.95f, "room");
  return true;
}
