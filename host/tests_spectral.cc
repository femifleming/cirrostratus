// SPECTRAL mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

Controls SpectralNeutral() {
  Controls c = DefaultControls();
  c.position = 0.0f;   // no thinning
  c.size = 0.0f;       // no snap
  c.density = 0.0f;    // no blur
  c.texture = 0.5f;    // flat
  c.pitch = 0.0f;
  return c;
}

std::vector<uint8_t> Slot(Module& m) {
  PersistentChunk chunks[kMaxPersistentChunks + 1];
  size_t n = m.engine().GetPersistentData(chunks);
  std::vector<uint8_t> slot;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
    slot.insert(slot.end(), p, p + chunks[i].size);
    while (slot.size() & 3) slot.push_back(0);
  }
  return slot;
}

}  // namespace

MONSOON_TEST(spectral_neutral_settings_pass_a_tone_at_unity) {
  Module m;
  CHECK(m.SetMode(MODE_SPECTRAL), "init");
  auto in = Sine(1000.0f, 2.0f, 0.3f);
  auto out = m.Render(in, [](size_t) { return SpectralNeutral(); });
  float a_in = ToneAmplitude(in, 1000.0f, 48000, 96000);
  float a_out = ToneAmplitude(out, 1000.0f, 48000, 96000);
  std::printf("    1 kHz in %.3f, out %.3f (%.2f dB)\n", a_in, a_out,
              Db(a_out / a_in));
  CHECK(std::fabs(Db(a_out / a_in)) < 1.0f, "gain %.2f dB", Db(a_out / a_in));
  return true;
}

MONSOON_TEST(spectral_pitch_transposes) {
  Module m;
  CHECK(m.SetMode(MODE_SPECTRAL), "init");
  auto in = Sine(1000.0f, 2.0f, 0.3f);
  auto out = m.Render(in, [](size_t) {
    Controls c = SpectralNeutral();
    c.pitch = 7.0f;
    return c;
  });
  float fifth = ToneAmplitude(out, 1498.3f, 48000, 96000);
  float orig = ToneAmplitude(out, 1000.0f, 48000, 96000);
  std::printf("    +7 st: 1498 Hz %.3f, 1000 Hz %.4f\n", fifth, orig);
  CHECK(fifth > 0.1f, "transposed tone missing");
  CHECK(fifth > orig * 20.0f, "original still present");
  return true;
}

MONSOON_TEST(spectral_freeze_holds_tone_after_input_stops) {
  Module m;
  CHECK(m.SetMode(MODE_SPECTRAL), "init");
  auto in = Sine(700.0f, 4.0f, 0.3f);
  for (size_t i = 72000; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
  auto out = m.Render(in, [](size_t b) {
    Controls c = SpectralNeutral();
    c.freeze = b * kBlockSize >= 48000;  // freeze at 1 s, input stops at 1.5 s
    return c;
  });
  float held = ToneAmplitude(out, 700.0f, 120000, 168000);
  float before = ToneAmplitude(out, 700.0f, 24000, 48000);
  std::printf("    700 Hz before freeze %.3f, 1 s after input stopped %.3f\n",
              before, held);
  CHECK(held > before * 0.5f, "frozen tone faded");
  return true;
}

MONSOON_TEST(spectral_texture_tilts_the_spectrum) {
  float ratio[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_SPECTRAL), "init");
    std::vector<FloatFrame> in = Sine(300.0f, 2.0f, 0.2f);
    auto hi = Sine(6000.0f, 2.0f, 0.2f);
    for (size_t i = 0; i < in.size(); ++i) {
      in[i].l += hi[i].l;
      in[i].r += hi[i].r;
    }
    auto out = m.Render(in, [&](size_t) {
      Controls c = SpectralNeutral();
      c.texture = k == 0 ? 0.0f : 1.0f;
      return c;
    });
    ratio[k] = ToneAmplitude(out, 6000.0f, 48000, 96000) /
        ToneAmplitude(out, 300.0f, 48000, 96000);
  }
  float diff = Db(ratio[1]) - Db(ratio[0]);
  std::printf("    6 kHz vs 300 Hz: %.1f dB (dark) %.1f dB (bright), swing %.1f dB\n",
              Db(ratio[0]), Db(ratio[1]), diff);
  // 4.3 octaves x 18 dB/oct of swing = ~78 dB; allow slack.
  CHECK(diff > 50.0f, "tilt too weak: %.1f dB", diff);
  return true;
}

MONSOON_TEST(spectral_size_snaps_to_harmonics) {
  Module m;
  CHECK(m.SetMode(MODE_SPECTRAL), "init");
  auto in = Sine(300.0f, 2.0f, 0.3f);
  auto out = m.Render(in, [](size_t) {
    Controls c = SpectralNeutral();
    c.size = 1.0f;  // full snap onto harmonics of 65.41 Hz: 300 -> 327 Hz
    return c;
  });
  float snapped = ToneAmplitude(out, 327.05f, 48000, 96000);
  float orig = ToneAmplitude(out, 300.0f, 48000, 96000);
  std::printf("    327 Hz (5th harmonic) %.3f, 300 Hz %.3f\n", snapped, orig);
  CHECK(snapped > orig * 3.0f, "not snapped");
  return true;
}

MONSOON_TEST(spectral_save_load_and_morph) {
  // Freeze a 1 kHz tone and save it.
  std::vector<uint8_t> slot;
  {
    Module a;
    CHECK(a.SetMode(MODE_SPECTRAL), "init");
    auto in = Sine(1000.0f, 1.5f, 0.3f);
    a.Render(in, [](size_t b) {
      Controls c = SpectralNeutral();
      c.freeze = b * kBlockSize >= 48000;
      return c;
    });
    slot = Slot(a);
    CHECK(!slot.empty(), "nothing saved");
  }
  // Loading plays the saved spectrum even with silence in.
  {
    Module b;
    b.engine().RequestLoad(slot.data(), slot.size());
    auto out = b.Render(Silence(2.0f), [](size_t) { return SpectralNeutral(); });
    CHECK(b.engine().mode() == MODE_SPECTRAL, "mode");
    float a1k = ToneAmplitude(out, 1000.0f, 48000, 96000);
    std::printf("    loaded slot plays 1 kHz at %.3f\n", a1k);
    CHECK(a1k > 0.05f, "loaded spectrum silent");
  }
  // Morph: frozen 500 Hz, slot = 1 kHz; POSITION 0 vs 1.
  float ratio[2];
  for (int k = 0; k < 2; ++k) {
    Module c;
    CHECK(c.SetMode(MODE_SPECTRAL), "init");
    CHECK(c.engine().supports_morph(), "morph support");
    c.engine().SetMorphSource(slot.data(), slot.size());
    auto in = Sine(500.0f, 2.5f, 0.3f);
    auto out = c.Render(in, [&](size_t b) {
      Controls ctl = SpectralNeutral();
      ctl.freeze = b * kBlockSize >= 48000;
      ctl.position = k == 0 ? 0.0f : 1.0f;
      return ctl;
    });
    ratio[k] = ToneAmplitude(out, 1000.0f, 72000, 120000) /
        (ToneAmplitude(out, 500.0f, 72000, 120000) + 1e-9f);
  }
  std::printf("    1k/500 ratio: position 0 -> %.1f dB, position 1 -> %.1f dB\n",
              Db(ratio[0]), Db(ratio[1]));
  CHECK(ratio[0] < 0.1f && ratio[1] > 10.0f, "morph did not move to the slot");
  return true;
}

MONSOON_TEST(spectral_bal_fbk_rvb_do_what_they_say) {
  auto run = [](const std::vector<FloatFrame>& in, float bal, float fbk,
                float rvb) {
    Module m;
    m.SetMode(MODE_SPECTRAL);
    return m.Render(in, [&](size_t) {
      Controls c = SpectralNeutral();
      c.bal = bal; c.fbk = fbk; c.rvb = rvb;
      return c;
    });
  };
  auto tone = Sine(1000.0f, 2.0f, 0.3f);
  // BAL: a linear shift, the same number of Hz whatever the frequency.
  // Knob 0.8 -> x = 0.588 -> 1000 * (2^(8x) - 1) / 255 ~ 98 Hz.
  const float x = (0.6f - 0.03f) / 0.97f;
  const float hz = 1000.0f * (std::exp2(8.0f * x) - 1.0f) / 255.0f;
  auto up = run(tone, 0.8f, 0.0f, 0.0f);
  auto down = run(tone, 0.2f, 0.0f, 0.0f);
  float up_new = ToneAmplitude(up, 1000.0f + hz, 48000, 96000);
  float up_old = ToneAmplitude(up, 1000.0f, 48000, 96000);
  float dn_new = ToneAmplitude(down, 1000.0f - hz, 48000, 96000);
  float dn_old = ToneAmplitude(down, 1000.0f, 48000, 96000);
  std::printf("    BAL +/-%.0f Hz: up %.3f (1 kHz %.4f), down %.3f (1 kHz %.4f)\n",
              hz, up_new, up_old, dn_new, dn_old);
  CHECK(up_new > 0.1f && up_new > 10.0f * up_old, "no upward shift");
  CHECK(dn_new > 0.1f && dn_new > 10.0f * dn_old, "no downward shift");

  // FBK: the tone stops at 1 s; 0.5 s later it is still there.
  auto burst = Sine(1000.0f, 2.0f, 0.3f);
  for (size_t i = 48000; i < burst.size(); ++i) burst[i].l = burst[i].r = 0.0f;
  auto dry = run(burst, 0.5f, 0.0f, 0.0f);
  auto held = run(burst, 0.5f, 1.0f, 0.0f);
  float tail_dry = ToneAmplitude(dry, 1000.0f, 72000, 96000);
  float tail_fb = ToneAmplitude(held, 1000.0f, 72000, 96000);
  float body_fb = ToneAmplitude(held, 1000.0f, 24000, 48000);
  std::printf("    FBK: 0.5-1 s after the tone: neutral %.4f, full %.3f (body %.3f)\n",
              tail_dry, tail_fb, body_fb);
  CHECK(tail_fb > 0.1f && tail_fb > 50.0f * tail_dry, "FBK does not sustain");
  CHECK(body_fb < 0.4f, "FBK made the steady tone louder");

  // RVB: the sine's energy no longer sits in one coherent line.
  auto flat = run(tone, 0.5f, 0.0f, 0.0f);
  auto wash = run(tone, 0.5f, 0.0f, 1.0f);
  auto purity = [](const std::vector<FloatFrame>& v) {
    Stats s = Measure(v, 48000, 96000);
    return ToneAmplitude(v, 1000.0f, 48000, 96000) / (s.rms * 1.41421f + 1e-9f);
  };
  float p_flat = purity(flat), p_wash = purity(wash);
  float lvl_flat = Measure(flat, 48000, 96000).rms;
  float lvl_wash = Measure(wash, 48000, 96000).rms;
  std::printf("    RVB: tonal share neutral %.2f, full %.2f; rms %.3f -> %.3f\n",
              p_flat, p_wash, lvl_flat, lvl_wash);
  CHECK(p_flat > 0.9f && p_wash < 0.7f * p_flat, "RVB does not diffuse");
  CHECK(lvl_wash < 1.5f * lvl_flat, "RVB louder");
  return true;
}
