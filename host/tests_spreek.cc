// SPREEK mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

float BandRms(const std::vector<FloatFrame>& v, float hz, size_t from,
              size_t to, float q = 4.0f) {
  Svf f;
  f.Init();
  f.set(hz / 48000.0f, q);
  double sum = 0;
  for (size_t i = 0; i < to; ++i) {
    float y = f.BpNormalized(v[i].l);
    if (i >= from) sum += double(y) * y;
  }
  return float(std::sqrt(sum / (to - from)));
}

std::vector<FloatFrame> RenderSpreek(const std::vector<FloatFrame>& in,
                                     std::function<void(size_t, Controls*)> f) {
  Module m;
  m.SetMode(MODE_SPREEK);
  return m.Render(in, [&](size_t b) {
    Controls c = DefaultControls();
    c.texture = 0.0f;
    c.size = 0.5f;   // formants x ~1
    f(b, &c);
    return c;
  });
}

const float kVowelA = 0.15f;
const float kVowelI = 0.15f + 0.85f * 0.5f;

}  // namespace

MONSOON_TEST(spreek_vowels_put_formants_in_the_right_place) {
  auto in = Music(3.0f);
  float a_f1[2], a_f2[2];  // [vowel a, vowel i] energy near the two F2s
  for (int k = 0; k < 2; ++k) {
    auto out = RenderSpreek(in, [&](size_t, Controls* c) {
      c->position = k == 0 ? kVowelA : kVowelI;
      c->density = 1.0f;   // internal glottal source
      c->size = 0.515f;    // scale 0.7 * 2^0.515 = 1.0
    });
    // Harmonics 8 (1046 Hz, near a's F2) and 14 (1831 Hz, near i's F2).
    a_f1[k] = BandRms(out, 8 * 130.81f, 48000, out.size(), 30.0f);
    a_f2[k] = BandRms(out, 14 * 130.81f, 48000, out.size(), 30.0f);
    CHECK(Measure(out).finite, "finite");
  }
  float a_tilt = Db(a_f1[0]) - Db(a_f2[0]);
  float i_tilt = Db(a_f1[1]) - Db(a_f2[1]);
  std::printf("    1046 Hz minus 1831 Hz: vowel a %+.1f dB, vowel i %+.1f dB\n",
              a_tilt, i_tilt);
  CHECK(a_tilt - i_tilt > 10.0f, "a and i should have very different formants");
  return true;
}

MONSOON_TEST(spreek_carrier_follows_pitch) {
  auto in = Sine(1000.0f, 2.0f, 0.3f);
  float at[2][2];
  for (int k = 0; k < 2; ++k) {
    auto out = RenderSpreek(in, [&](size_t, Controls* c) {
      c->position = kVowelA;
      c->density = 1.0f;
      c->pitch = k == 0 ? 0.0f : 12.0f;
    });
    at[k][0] = ToneAmplitude(out, 130.81f, 48000, out.size());
    at[k][1] = ToneAmplitude(out, 261.63f, 48000, out.size());
  }
  std::printf("    pitch 0: 130.8 Hz %.1f dB | pitch +12: 130.8 Hz %.1f dB, 261.6 Hz %.1f dB\n",
              Db(at[0][0]), Db(at[1][0]), Db(at[1][1]));
  CHECK(Db(at[0][0]) > -50.0f, "no carrier at C3");
  CHECK(Db(at[1][1]) - Db(at[1][0]) > 30.0f, "carrier did not move up an octave");
  return true;
}

MONSOON_TEST(spreek_talkbox_takes_the_inputs_envelope) {
  // A dark input and a bright input give the carrier a dark / bright shape.
  float low[2], high[2];
  for (int k = 0; k < 2; ++k) {
    auto in = Sine(k == 0 ? 400.0f : 3000.0f, 2.0f, 0.3f);
    auto out = RenderSpreek(in, [&](size_t, Controls* c) {
      c->position = 0.0f;   // input's own envelope
      c->density = 1.0f;    // internal carrier
      c->pitch = -12.0f;    // 65 Hz: dense harmonics
    });
    low[k] = BandRms(out, 400.0f, 48000, out.size());
    high[k] = BandRms(out, 3000.0f, 48000, out.size());
    CHECK(Measure(out).finite, "finite");
    CHECK(Db(Measure(out, 48000).rms) > -40.0f, "talkbox silent");
  }
  float dark = Db(low[0]) - Db(high[0]);
  float bright = Db(low[1]) - Db(high[1]);
  std::printf("    400 Hz minus 3 kHz: dark input %+.1f dB, bright input %+.1f dB\n",
              dark, bright);
  CHECK(dark - bright > 20.0f, "envelope not transferred");
  return true;
}

MONSOON_TEST(spreek_talkbox_with_input_source_passes_the_input) {
  auto in = Music(3.0f);
  auto out = RenderSpreek(in, [&](size_t, Controls* c) {
    c->position = 0.0f;
    c->density = 0.0f;
  });
  // Residual through the matching synthesis filter reconstructs the input.
  float lvl_in = Measure(in, 48000).rms, lvl_out = Measure(out, 48000).rms;
  std::printf("    input %.1f dB, output %.1f dB\n", Db(lvl_in), Db(lvl_out));
  CHECK(std::fabs(Db(lvl_in) - Db(lvl_out)) < 6.0f, "level should be kept");
  return true;
}

MONSOON_TEST(spreek_freeze_drones_and_trig_bursts) {
  auto in = Music(1.0f);
  auto s = Silence(3.0f);
  in.insert(in.end(), s.begin(), s.end());
  const size_t trig_block = size_t(3.5f * 48000) / kBlockSize;
  auto out = RenderSpreek(in, [&](size_t b, Controls* c) {
    c->position = kVowelA;
    c->freeze = b * kBlockSize > 40000 && b < trig_block - 300;
    c->trigger = b == trig_block;
  });
  float drone = Measure(out, 2 * 48000, 3 * 48000).rms;
  float burst = BandRms(out, 4200.0f, size_t(3.5f * 48000), size_t(3.6f * 48000), 1.5f);
  float quiet = BandRms(out, 4200.0f, size_t(3.3f * 48000), size_t(3.4f * 48000), 1.5f);
  std::printf("    frozen drone %.1f dB; 4.2 kHz band: before TRIG %.1f dB, burst %.1f dB\n",
              Db(drone), Db(quiet), Db(burst));
  CHECK(Db(drone) > -40.0f, "frozen phoneme should drone");
  CHECK(Db(burst) - Db(quiet) > 20.0f, "TRIG should make a consonant burst");
  return true;
}
