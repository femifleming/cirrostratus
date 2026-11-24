// CROSS mode behaviour (left = modulator, right = carrier).

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

// Left/right from two independent mono generators.
std::vector<FloatFrame> Stereo(const std::vector<FloatFrame>& l,
                               const std::vector<FloatFrame>& r) {
  std::vector<FloatFrame> v(std::min(l.size(), r.size()));
  for (size_t i = 0; i < v.size(); ++i) {
    v[i].l = l[i].l;
    v[i].r = r[i].l;
  }
  return v;
}

std::vector<FloatFrame> Saw(float hz, float seconds, float amp) {
  std::vector<FloatFrame> v(size_t(seconds * 48000) & ~size_t(31));
  double ph = 0;
  for (auto& f : v) {
    ph += hz / 48000.0;
    if (ph >= 1.0) ph -= 1.0;
    f.l = f.r = amp * float(2.0 * ph - 1.0);
  }
  return v;
}

Controls Vocoder() {
  Controls c = DefaultControls();
  c.position = 0.0f;
  c.density = 0.0f;   // fine resolution
  c.texture = 0.2f;
  c.size = 0.5f;      // no formant shift
  c.pitch = 0.0f;
  return c;
}

}  // namespace

MONSOON_TEST(cross_vocoder_imposes_the_modulator_envelope) {
  Module m;
  CHECK(m.SetMode(MODE_CROSS), "init");
  auto in = Stereo(Sine(1540.0f, 2.0f, 0.3f), Saw(110.0f, 2.0f, 0.3f));
  auto out = m.Render(in, [](size_t) { return Vocoder(); });
  // In the saw, harmonic 4 (440 Hz) is 3.5x louder than harmonic 14.
  float h14 = ToneAmplitude(out, 1540.0f, 48000, 96000);
  float h4 = ToneAmplitude(out, 440.0f, 48000, 96000);
  std::printf("    harmonic 14 %.4f, harmonic 4 %.4f\n", h14, h4);
  CHECK(h14 > h4 * 3.0f, "modulator envelope not imposed");
  return true;
}

MONSOON_TEST(cross_silent_modulator_gives_silence) {
  Module m;
  CHECK(m.SetMode(MODE_CROSS), "init");
  auto in = Stereo(Silence(2.0f), Saw(110.0f, 2.0f, 0.3f));
  auto out = m.Render(in, [](size_t) { return Vocoder(); });
  Stats s = Measure(out, 48000);
  std::printf("    rms with silent modulator %.1f dBFS\n", Db(s.rms));
  CHECK(Db(s.rms) < -60.0f, "carrier leaks through");
  return true;
}

MONSOON_TEST(cross_freeze_holds_the_modulator) {
  Module m;
  CHECK(m.SetMode(MODE_CROSS), "init");
  auto mod = Sine(1540.0f, 3.0f, 0.3f);
  for (size_t i = 72000; i < mod.size(); ++i) mod[i].l = mod[i].r = 0.0f;
  auto in = Stereo(mod, Saw(110.0f, 3.0f, 0.3f));
  auto out = m.Render(in, [](size_t b) {
    Controls c = Vocoder();
    c.freeze = b * kBlockSize >= 48000;
    return c;
  });
  float held = ToneAmplitude(out, 1540.0f, 96000, 144000);
  float live = ToneAmplitude(out, 1540.0f, 24000, 48000);
  std::printf("    harmonic 14: live %.4f, held after modulator stopped %.4f\n",
              live, held);
  CHECK(held > live * 0.5f, "freeze did not hold the modulator");
  return true;
}

MONSOON_TEST(cross_position_one_uses_left_magnitudes) {
  Module m;
  CHECK(m.SetMode(MODE_CROSS), "init");
  auto in = Stereo(Sine(1000.0f, 2.0f, 0.3f), Noise(2.0f, 0.2f));
  auto out = m.Render(in, [](size_t) {
    Controls c = Vocoder();
    c.position = 1.0f;
    return c;
  });
  float tone = ToneAmplitude(out, 1000.0f, 48000, 96000);
  float off = ToneAmplitude(out, 3000.0f, 48000, 96000);
  std::printf("    1 kHz %.4f vs 3 kHz %.4f\n", tone, off);
  CHECK(tone > off * 10.0f, "left magnitudes not used");
  return true;
}

MONSOON_TEST(cross_trigger_holds_the_carrier) {
  Module m;
  CHECK(m.SetMode(MODE_CROSS), "init");
  auto car = Saw(110.0f, 3.0f, 0.3f);
  for (size_t i = 72000; i < car.size(); ++i) car[i].l = car[i].r = 0.0f;
  auto in = Stereo(Noise(3.0f, 0.2f), car);
  auto out = m.Render(in, [](size_t b) {
    Controls c = Vocoder();
    c.density = 0.5f;
    c.trigger = b * kBlockSize == 48000;  // hold the carrier at 1 s
    return c;
  });
  float held = ToneAmplitude(out, 440.0f, 96000, 144000);
  float live = ToneAmplitude(out, 440.0f, 24000, 48000);
  std::printf("    carrier harmonic: live %.4f, held after carrier stopped %.4f\n",
              live, held);
  CHECK(held > live * 0.3f, "carrier hold failed");
  return true;
}

MONSOON_TEST(cross_bal_fbk_rvb_do_what_they_say) {
  auto render = [](const std::vector<FloatFrame>& in, float bal, float fbk,
                   float pitch) {
    Module m;
    m.SetMode(MODE_CROSS);
    return m.Render(in, [&](size_t) {
      Controls c = Vocoder();
      c.bal = bal;
      c.fbk = fbk;
      c.pitch = pitch;
      return c;
    });
  };
  // BAL left: the saw carrier turns into noise; its harmonics disappear
  // from the output while the level stays.
  auto voice = Stereo(Noise(2.0f, 0.2f), Saw(110.0f, 2.0f, 0.3f));
  auto tonal = render(voice, 0.5f, 0.0f, 0.0f);
  auto whisper = render(voice, 0.0f, 0.0f, 0.0f);
  auto share = [](const std::vector<FloatFrame>& v) {
    float h = 0.0f;
    for (int k = 2; k <= 8; ++k) h += ToneAmplitude(v, 110.0f * k, 48000, 96000);
    return h / (Measure(v, 48000, 96000).rms + 1e-9f);
  };
  float s_tonal = share(tonal), s_whisper = share(whisper);
  float l_tonal = Measure(tonal, 48000, 96000).rms;
  float l_whisper = Measure(whisper, 48000, 96000).rms;
  std::printf("    BAL left: harmonic share %.2f -> %.2f, level %.1f -> %.1f dB\n",
              s_tonal, s_whisper, Db(l_tonal), Db(l_whisper));
  CHECK(s_whisper < 0.2f * s_tonal, "carrier not turned to noise");
  CHECK(l_whisper > 0.25f * l_tonal, "noise carrier too quiet");

  // BAL right: with no carrier at all, the buzz carries the modulator, and
  // PITCH plays it (+12 st -> 261.6 Hz fundamental).
  auto lonely = Stereo(Noise(2.0f, 0.2f), Silence(2.0f));
  auto none = render(lonely, 0.5f, 0.0f, 12.0f);
  auto buzz = render(lonely, 1.0f, 0.0f, 12.0f);
  float f0 = ToneAmplitude(buzz, 261.63f, 48000, 96000);
  float sub = ToneAmplitude(buzz, 130.81f, 48000, 96000);
  float silent = Measure(none, 48000, 96000).rms;
  std::printf("    BAL right: no carrier %.1f dB; buzz 261.6 Hz %.4f, 130.8 Hz %.4f\n",
              Db(silent), f0, sub);
  CHECK(Db(silent) < -60.0f, "carrier from nowhere at noon");
  CHECK(f0 > 0.002f && f0 > 10.0f * sub, "buzz not at the PITCH note");

  // FBK: formants sharpen: the modulator's 1540 Hz peak stands out further
  // from the neighbouring carrier harmonics.
  auto in = Stereo(Sine(1540.0f, 2.0f, 0.3f), Saw(110.0f, 2.0f, 0.3f));
  float contrast[2];
  for (int k = 0; k < 2; ++k) {
    auto out = render(in, 0.5f, k == 0 ? 0.0f : 1.0f, 0.0f);
    contrast[k] = ToneAmplitude(out, 1540.0f, 48000, 96000) /
        (ToneAmplitude(out, 1320.0f, 48000, 96000) + 1e-9f);
  }
  std::printf("    FBK: harmonic 14 over harmonic 12: %.1f dB -> %.1f dB\n",
              Db(contrast[0]), Db(contrast[1]));
  CHECK(Db(contrast[1]) - Db(contrast[0]) > 6.0f, "no formant emphasis");

  // RVB: the engine's FDN.
  Module m;
  CHECK(m.SetMode(MODE_CROSS), "init");
  Controls r = Vocoder();
  r.rvb = 0.6f;
  CHECK(m.engine().mode_instance(MODE_CROSS)->reverb_amount(r) == 0.6f,
        "RVB not mapped to the reverb");
  return true;
}
