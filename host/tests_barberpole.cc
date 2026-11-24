// BARBERPOLE mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/barberpole.h"

using namespace host;
using namespace monsoon;

namespace {

// POSITION that gives a +100 Hz shift (0.1 * 20000^((m - 0.02) / 0.98)).
const float kUp100 = 0.5f + 0.5f * (0.02f + 0.98f * 0.697519f);

Controls Base() {
  Controls c = DefaultControls();
  c.position = kUp100;
  c.size = 0.3f;
  c.density = 0.0f;
  c.texture = 0.0f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Run(const std::vector<FloatFrame>& in,
                            std::function<void(size_t, Controls*)> f,
                            Module* keep = nullptr) {
  Module local;
  Module& m = keep ? *keep : local;
  m.SetMode(MODE_BARBERPOLE);
  return m.Render(in, [&](size_t b) {
    Controls c = Base();
    f(b, &c);
    return c;
  });
}

}  // namespace

MONSOON_TEST(barberpole_one_pass_shifts_by_the_set_amount) {
  Module m;
  auto in = Sine(1000.0f, 2.0f, 0.4f);
  auto out = Run(in, [](size_t, Controls*) {}, &m);
  auto* mode = static_cast<BarberpoleMode*>(m.engine().mode_instance(MODE_BARBERPOLE));
  float up = ToneAmplitude(out, 1100.0f, 48000, out.size());
  float mirror = ToneAmplitude(out, 900.0f, 48000, out.size());
  float orig = ToneAmplitude(out, 1000.0f, 48000, out.size());
  std::printf("    shift %.1f Hz: 1100 Hz %.1f dB, 900 Hz %.1f dB, 1000 Hz %.1f dB\n",
              mode->shift_hz(0), Db(up), Db(mirror), Db(orig));
  CHECK(std::fabs(mode->shift_hz(0) - 100.0f) < 1.0f, "shift mapping");
  CHECK(up > 0.1f, "shifted tone missing");
  CHECK(Db(up) - Db(mirror) > 35.0f && Db(up) - Db(orig) > 35.0f, "not clean SSB");
  return true;
}

MONSOON_TEST(barberpole_feedback_builds_the_spiral) {
  auto in = Sine(1000.0f, 3.0f, 0.3f);
  auto out = Run(in, [](size_t, Controls* c) {
    c->density = 0.85f;
    c->size = 0.2f;
  });
  float steps[4];
  for (int k = 0; k < 4; ++k) {
    steps[k] = ToneAmplitude(out, 1100.0f + 100.0f * k, 96000, out.size());
  }
  std::printf("    1100 %.1f, 1200 %.1f, 1300 %.1f, 1400 %.1f dB\n",
              Db(steps[0]), Db(steps[1]), Db(steps[2]), Db(steps[3]));
  for (int k = 0; k < 4; ++k) CHECK(Db(steps[k]) > -40.0f, "step %d missing", k + 1);
  CHECK(Measure(out).finite, "finite");
  return true;
}

MONSOON_TEST(barberpole_trigger_flips_and_texture_splits) {
  auto in = Sine(1000.0f, 3.0f, 0.4f);
  const size_t flip = 48000 / kBlockSize;
  auto flipped = Run(in, [&](size_t b, Controls* c) { c->trigger = b == flip; });
  float down = ToneAmplitude(flipped, 900.0f, 96000, flipped.size());
  float up = ToneAmplitude(flipped, 1100.0f, 96000, flipped.size());
  std::printf("    after TRIG: 900 Hz %.1f dB, 1100 Hz %.1f dB\n", Db(down), Db(up));
  CHECK(Db(down) - Db(up) > 35.0f, "TRIG should flip the direction");

  auto split = Run(in, [](size_t, Controls* c) { c->texture = 1.0f; });
  float l_up = ToneAmplitude(split, 1100.0f, 48000, split.size(), true);
  float r_down = ToneAmplitude(split, 900.0f, 48000, split.size(), false);
  float r_up = ToneAmplitude(split, 1100.0f, 48000, split.size(), false);
  std::printf("    texture 1: L 1100 %.1f dB, R 900 %.1f dB, R 1100 %.1f dB\n",
              Db(l_up), Db(r_down), Db(r_up));
  CHECK(l_up > 0.05f && r_down > 0.05f, "split missing");
  CHECK(Db(r_down) - Db(r_up) > 30.0f, "right should go down");
  return true;
}

MONSOON_TEST(barberpole_size_sets_the_loop_delay) {
  // A click comes back after the loop delay (size 0.5 -> 72 * (24000/72)^0.5).
  auto in = Silence(1.0f);
  for (int i = 0; i < 16; ++i) in[4800 + i].l = in[4800 + i].r = 0.8f;
  auto out = Run(in, [](size_t, Controls* c) {
    c->size = 0.5f;
    c->position = 0.5f;  // no shift
  });
  size_t peak = 4800;
  for (size_t i = 4800; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > std::fabs(out[peak].l)) peak = i;
  }
  float expected = 72.0f * std::sqrt(24000.0f / 72.0f);
  std::printf("    echo after %zu samples (expected ~%.0f)\n", peak - 4800, expected);
  CHECK(std::fabs(float(peak - 4800) - expected) < 40.0f, "loop delay");
  return true;
}

MONSOON_TEST(barberpole_freeze_keeps_spiralling_without_input) {
  auto in = Sine(500.0f, 1.0f, 0.4f);
  auto s = Silence(3.0f);
  in.insert(in.end(), s.begin(), s.end());
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(in, [&](size_t b, Controls* c) {
      c->position = 0.5f + 0.5f * (0.02f + 0.98f * 0.4f);  // ~5 Hz
      c->density = 0.5f;
      c->size = 0.6f;
      c->freeze = k == 1 && b * kBlockSize > 40000;
    });
    tail[k] = Measure(out, 3 * 48000, out.size()).rms;
  }
  std::printf("    rms at 3-4 s: normal %.1f dB, frozen %.1f dB\n", Db(tail[0]), Db(tail[1]));
  CHECK(Db(tail[1]) > -30.0f, "frozen spiral died");
  CHECK(Db(tail[1]) - Db(tail[0]) > 30.0f, "freeze should sustain");
  return true;
}
