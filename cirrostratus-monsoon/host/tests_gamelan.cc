// GAMELAN behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/gamelan.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.rvb = 0.0f;
  c.size = 0.0f;      // no ombak
  c.texture = 0.0f;   // saron
  c.density = 0.7f;
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

float KeyPosition(int k) { return (k + 0.5f) / 7.0f; }

std::vector<FloatFrame> StrikeKey(Controls base, int key, float seconds,
                                  int freeze_block = -1, GamelanMode** hook = NULL,
                                  Module* keep = NULL) {
  Module local;
  Module& m = keep ? *keep : local;
  m.SetMode(MODE_GAMELAN, base.option);
  auto in = Silence(seconds);
  auto out = m.Render(in, [&](size_t b) {
    Controls c = base;
    c.position = KeyPosition(key);
    c.trigger = b == 4;
    c.freeze = freeze_block >= 0 && int(b) >= freeze_block;
    return c;
  });
  if (hook) *hook = static_cast<GamelanMode*>(m.engine().mode_instance(MODE_GAMELAN));
  return out;
}

}  // namespace

MONSOON_TEST(gamelan_keys_follow_pelog_and_slendro) {
  const float cents[2][7] = {
    { 0, 120, 258, 539, 675, 785, 943 },
    { 0, 231, 474, 717, 955, 1200, 1431 },
  };
  for (int opt = 0; opt < 2; ++opt) {
    for (int k = 0; k < 7; ++k) {
      Controls c = Base();
      c.option = uint8_t(opt);
      auto out = StrikeKey(c, k, 1.0f);
      float expect = 261.63f * std::pow(2.0f, cents[opt][k] / 1200.0f);
      float found = PeakNear(out, expect, 30.0f, 2400, 48000 - 64);
      float err = 1200.0f * std::log2(found / expect);
      CHECK(std::fabs(err) <= 3.0f, "%s key %d off by %.1f cents",
            opt ? "slendro" : "pelog", k, err);
    }
    std::printf("    %s: all 7 keys within 3 cents\n", opt ? "slendro" : "pelog");
  }
  return true;
}

MONSOON_TEST(gamelan_ombak_beats_at_size_rate) {
  float twin[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.size = k == 0 ? 0.0f : 0.5f;   // 0 / 5 Hz
    c.density = 0.9f;
    auto out = StrikeKey(c, 0, 2.0f);
    float f = 261.63f;
    twin[k] = ToneAmplitude(out, f + 5.0f, 4800, 48000 * 2 - 64) /
        ToneAmplitude(out, f, 4800, 48000 * 2 - 64);
  }
  std::printf("    energy at f+5 Hz relative to f: %.2f (no ombak), %.2f (5 Hz ombak)\n",
              twin[0], twin[1]);
  CHECK(twin[1] > 0.5f, "the twin should sound 5 Hz above");
  CHECK(twin[1] > twin[0] * 3.0f, "and only with ombak");
  return true;
}

MONSOON_TEST(gamelan_gong_end_drops_two_octaves) {
  Controls c = Base();
  c.texture = 1.0f;
  GamelanMode* g = NULL;
  Module m;
  auto out = StrikeKey(c, 0, 2.0f, -1, &g, &m);
  float expect = 261.63f / 4.0f;
  float found = PeakNear(out, expect, 30.0f, 4800, 48000 * 2 - 64);
  std::printf("    gong key 1: model %.2f Hz, heard %.2f Hz\n", g->key_hz(0), found);
  CHECK(std::fabs(g->key_hz(0) - expect) < 0.1f, "model");
  CHECK(std::fabs(1200.0f * std::log2(found / expect)) < 5.0f, "heard");
  return true;
}

MONSOON_TEST(gamelan_density_sets_decay_and_freeze_holds) {
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = k == 0 ? 0.2f : 0.8f;
    auto out = StrikeKey(c, 3, 3.0f);
    tail[k] = Measure(out, 48000 * 2, 48000 * 2 + 9600).rms /
        Measure(out, 0, 4800).rms;
  }
  Controls c = Base();
  c.density = 0.0f;
  auto frozen = StrikeKey(c, 3, 4.0f, 6);
  float a = Measure(frozen, 4800, 9600).rms;
  float b = Measure(frozen, 48000 * 3, 48000 * 4 - 64).rms;
  std::printf("    after 2 s: %.1f dB (density 0.2) vs %.1f dB (0.8); frozen %.1f -> %.1f dB\n",
              Db(tail[0]), Db(tail[1]), Db(a), Db(b));
  CHECK(tail[1] > tail[0] * 10.0f, "density should lengthen the ring");
  CHECK(b > a * 0.5f, "freeze should ring on");
  return true;
}

MONSOON_TEST(gamelan_input_excites_bank) {
  Module m;
  m.SetMode(MODE_GAMELAN);
  auto in = Music(2.0f);
  auto out = m.Render(in, [](size_t) { return Base(); });
  Stats s = Measure(out, 24000, out.size());
  std::printf("    music through the bank: %.1f dBFS\n", Db(s.rms));
  CHECK(Db(s.rms) > -45.0f && s.finite && s.peak <= 1.0f, "input excitation");
  return true;
}

// Per-mode knobs: BAL = mallet hardness, FBK = tutup (choke the previous
// key on each strike), RVB = the engine's FDN (a pendopo).
MONSOON_TEST(gamelan_bal_fbk_rvb_do_what_they_say) {
  // BAL: third partial (5.40 f) relative to the fundamental, key 3.
  const float f = 261.63f * std::pow(2.0f, 539.0f / 1200.0f);
  float tilt[3];
  const float bals[3] = { 0.0f, 0.5f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    Controls c = Base();
    c.bal = bals[k];
    auto out = StrikeKey(c, 3, 0.5f);
    tilt[k] = Db(ToneAmplitude(out, f * 5.40f, 2400, 12000)) -
              Db(ToneAmplitude(out, f, 2400, 12000));
  }
  std::printf("    BAL: partial 3 vs fundamental: soft %+.1f dB, noon %+.1f dB, hard %+.1f dB\n",
              tilt[0], tilt[1], tilt[2]);
  CHECK(tilt[1] - tilt[0] > 6.0f, "soft mallet should dull the strike");
  CHECK(tilt[2] - tilt[1] > 6.0f, "hard mallet should brighten the strike");

  // FBK: strike key 0, then key 4 at 0.3 s; key 0's ring after that.
  const float f0 = 261.63f;
  float ring[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_GAMELAN);
    auto out = m.Render(Silence(1.0f), [&](size_t b) {
      Controls c = Base();
      c.fbk = k ? 1.0f : 0.0f;
      const bool second = b >= 450;
      c.position = KeyPosition(second ? 4 : 0);
      c.trigger = b == 4 || b == 450;
      return c;
    });
    ring[k] = Db(ToneAmplitude(out, f0, 24000, 33600));
  }
  std::printf("    FBK: key 0 after key 4 is struck: off %.1f dB, full %.1f dB\n",
              ring[0], ring[1]);
  CHECK(ring[0] > -40.0f, "key 0 should ring on without tutup");
  CHECK(ring[1] < ring[0] - 40.0f, "tutup should choke the previous key");

  // RVB: short decay; a tail long after the bank has died.
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Controls c = Base();
    c.density = 0.0f;
    c.rvb = k ? 1.0f : 0.0f;
    auto out = StrikeKey(c, 2, 2.5f);
    tail[k] = Db(Measure(out, 72000, 120000 - 64).rms);
  }
  std::printf("    RVB: 1.5-2.5 s after a 0.3 s strike: off %.1f dB, full %.1f dB\n",
              tail[0], tail[1]);
  CHECK(tail[1] > tail[0] + 40.0f && tail[1] > -80.0f, "RVB should add a pendopo tail");

  // Fast random knobs with a stream of strikes: bounded.
  {
    Module m;
    m.SetMode(MODE_GAMELAN);
    Random r;
    r.Seed(4);
    auto out = m.Render(Music(4.0f), [&](size_t b) {
      Controls c = Base();
      c.density = 1.0f;
      c.position = r.Float();
      c.trigger = b % 50 == 0;
      c.bal = r.Float(); c.fbk = r.Float(); c.rvb = r.Float();
      return c;
    });
    Stats st = Measure(out);
    std::printf("    random CV + strikes: peak %.2f\n", st.peak);
    CHECK(st.finite && st.peak <= 1.0f, "bounded");
  }
  return true;
}
