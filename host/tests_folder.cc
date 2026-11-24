// FOLDER behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/folder.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.rvb = 0.0f;
  c.position = 0.3f;
  c.size = 0.5f;
  c.density = 0.5f;
  c.texture = 0.3f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Run(Controls base, float seconds, bool freeze,
                            int trig_block = -1,
                            const std::vector<FloatFrame>* input = NULL) {
  Module m;
  m.SetMode(MODE_FOLDER, base.option);
  std::vector<FloatFrame> in = input ? *input : Silence(seconds);
  return m.Render(in, [&](size_t b) {
    Controls c = base;
    c.freeze = freeze;
    c.trigger = int(b) == trig_block;
    return c;
  });
}

// Share of the power that sits on harmonics of f (up to 23 kHz).
float HarmonicShare(const std::vector<FloatFrame>& v, float f, size_t from,
                    size_t to) {
  double total = 0;
  for (size_t i = from; i < to; ++i) total += double(v[i].l) * v[i].l;
  total /= double(to - from);
  double harm = 0;
  for (int k = 1; k * f < 23000.0f; ++k) {
    float a = ToneAmplitude(v, k * f, from, to);
    harm += 0.5 * a * a;
  }
  return float(harm / (total + 1e-30));
}

float HfShare(const std::vector<FloatFrame>& v, size_t from, size_t to) {
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

}  // namespace

MONSOON_TEST(folder_gate_closed_is_silent) {
  auto out = Run(Base(), 1.0f, false);
  Stats s = Measure(out, 9600, out.size());
  std::printf("    closed gate, sine running: %.1f dBFS\n", Db(s.rms));
  CHECK(Db(s.rms) < -60.0f, "the LPG should keep the sine shut");
  return true;
}

MONSOON_TEST(folder_trig_pings_and_texture_sets_decay) {
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.texture = k ? 0.8f : 0.1f;
    auto out = Run(c, 2.0f, false, 4);
    float head = Measure(out, 0, 2400).rms;
    tail[k] = Measure(out, 24000, 28800).rms / head;
    CHECK(Db(head) > -30.0f, "ping should open the gate");
  }
  std::printf("    level 0.5 s after a ping: %.1f dB (short), %.1f dB (long)\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(tail[1] > tail[0] * 10.0f, "texture should lengthen the vactrol decay");
  return true;
}

MONSOON_TEST(folder_freeze_drones_at_pitch) {
  Controls c = Base();
  c.pitch = 7.0f;
  auto out = Run(c, 1.0f, true);
  float f = 130.81f * std::pow(2.0f, 7.0f / 12.0f);
  float on = ToneAmplitude(out, f, 9600, 48000 - 64);
  float off = ToneAmplitude(out, f * 1.06f, 9600, 48000 - 64);
  std::printf("    frozen drone at %.1f Hz: %.1f dB above off-pitch\n", f, Db(on) - Db(off));
  CHECK(on > off * 30.0f, "drone at PITCH");
  return true;
}

MONSOON_TEST(folder_position_folds_more) {
  float hf[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.position = k ? 1.0f : 0.0f;
    c.density = 1.0f;   // VCA only: no low-pass colouring
    auto out = Run(c, 1.0f, true);
    hf[k] = HfShare(out, 9600, 48000);
  }
  std::printf("    energy above 2 kHz: %.4f unfolded, %.4f folded\n", hf[0], hf[1]);
  CHECK(hf[1] > hf[0] * 20.0f, "folding should add bright harmonics");
  return true;
}

MONSOON_TEST(folder_size_adds_even_harmonics) {
  float second[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.size = k ? 0.9f : 0.5f;
    c.density = 1.0f;
    auto out = Run(c, 1.0f, true);
    float f = 130.81f;
    second[k] = ToneAmplitude(out, 2 * f, 9600, 48000 - 64) /
        ToneAmplitude(out, f, 9600, 48000 - 64);
  }
  std::printf("    2nd harmonic: %.1f dB symmetric, %.1f dB biased\n",
              Db(second[0]), Db(second[1]));
  CHECK(second[0] < 0.01f, "symmetric folding: no even harmonics");
  CHECK(second[1] > second[0] * 10.0f, "bias adds even harmonics");
  return true;
}

MONSOON_TEST(folder_oversampling_keeps_aliases_down) {
  Controls c = Base();
  c.position = 1.0f;
  c.density = 1.0f;
  c.pitch = 24.0f;   // 523 Hz, folded 10x
  auto out = Run(c, 1.0f, true);
  float share = HarmonicShare(out, 523.25f, 9600, 48000 - 64);
  std::printf("    power on harmonics: %.4f (aliases %.1f dB)\n", share,
              10.0f * std::log10(std::max(1e-9f, 1.0f - share)));
  CHECK(share > 0.99f, "too much aliasing");
  return true;
}

MONSOON_TEST(folder_density_lowpass_vs_vca) {
  // Half-open gate: low-pass mode is darker than VCA mode.
  float hf[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.position = 0.8f;
    c.density = k ? 1.0f : 0.0f;
    c.texture = 0.5f;
    auto out = Run(c, 1.0f, false, 4);
    hf[k] = HfShare(out, 19200, 33600);   // 0.4-0.7 s: gate half closed
  }
  std::printf("    decaying ping, energy above 2 kHz: %.4f low-pass, %.4f VCA\n", hf[0], hf[1]);
  CHECK(hf[1] > hf[0] * 2.0f, "low-pass gate should be darker");
  return true;
}

MONSOON_TEST(folder_input_only_option_folds_input) {
  Controls c = Base();
  c.option = 1;
  auto music = Music(2.0f);
  auto out = Run(c, 2.0f, false, -1, &music);
  auto quiet = Run(c, 1.0f, true);
  Stats s = Measure(out, 24000, out.size());
  Stats q = Measure(quiet, 9600, quiet.size());
  std::printf("    input-only: music %.1f dBFS, frozen silence %.1f dBFS\n",
              Db(s.rms), Db(q.rms));
  CHECK(Db(s.rms) > -40.0f, "input should come through");
  CHECK(Db(q.rms) < -80.0f, "no internal sine in option 1");
  return true;
}
