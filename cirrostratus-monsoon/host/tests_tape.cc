// TAPE mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/tape.h"

using namespace host;
using namespace monsoon;

namespace {

Controls TapeControls() {
  Controls c = DefaultControls();
  c.position = 0.02f;  // head 1 only
  c.density = 0.0f;    // no repeats
  c.texture = 0.0f;    // no wow/flutter
  c.size = 0.5f;       // 1x speed
  return c;
}

// Index of the largest |sample| at or after `from`.
size_t PeakIndex(const std::vector<FloatFrame>& v, size_t from) {
  size_t best = from;
  for (size_t i = from; i < v.size(); ++i) {
    if (std::fabs(v[i].l) > std::fabs(v[best].l)) best = i;
  }
  return best;
}

float MeasureEchoDelay(float size, uint8_t option, float* expected) {
  Module m;
  m.SetMode(MODE_TAPE, option);
  auto in = Silence(4.0f);
  const size_t click = 48000 / 2;
  for (int i = 0; i < 8; ++i) in[click + i].l = in[click + i].r = 0.8f;
  auto out = m.Render(in, [&](size_t) {
    Controls c = TapeControls();
    c.size = size;
    c.option = option;
    return c;
  });
  TapeMode* tape = static_cast<TapeMode*>(m.engine().mode_instance(MODE_TAPE));
  *expected = tape->head_delay_seconds(0);
  return (PeakIndex(out, click + 64) - click) / 48000.0f;
}

}  // namespace

MONSOON_TEST(tape_echo_arrives_at_the_head_distance) {
  float expected;
  float measured = MeasureEchoDelay(0.5f, 0, &expected);
  std::printf("    1x stereo: head 1 echo at %.4f s (expected %.4f s)\n",
              measured, expected);
  CHECK(std::fabs(measured - expected) < 0.0005f, "echo timing");
  CHECK(expected > 0.15f, "head spacing too small");
  return true;
}

MONSOON_TEST(tape_slower_speed_means_longer_delay) {
  float e1, e2;
  float d1 = MeasureEchoDelay(0.5f, 0, &e1);
  float d2 = MeasureEchoDelay(0.25f, 0, &e2);   // speed 0.5
  std::printf("    speed 1: %.4f s, speed 0.5: %.4f s (ratio %.3f)\n",
              d1, d2, d2 / d1);
  CHECK(std::fabs(d2 / d1 - 2.0f) < 0.02f, "half speed should double the delay");
  return true;
}

MONSOON_TEST(tape_mono_option_doubles_the_tape) {
  float es, em;
  MeasureEchoDelay(0.5f, 0, &es);
  float dm = MeasureEchoDelay(0.5f, 1, &em);
  std::printf("    head 1: stereo %.3f s, mono %.3f s (measured %.3f s)\n",
              es, em, dm);
  CHECK(em > es * 1.8f, "mono tape should be about twice as long");
  CHECK(std::fabs(dm - em) < 0.001f, "mono echo timing");
  return true;
}

MONSOON_TEST(tape_slow_tape_is_darker) {
  // Same 9 kHz tone through 1x and 0.25x tape: slow tape loses it.
  float level[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_TAPE);
    auto in = Sine(9000.0f, 5.0f, 0.4f);
    auto out = m.Render(in, [&](size_t) {
      Controls c = TapeControls();
      c.size = k == 0 ? 0.5f : 0.0f;
      return c;
    });
    level[k] = Measure(out, 4 * 48000, out.size()).rms;
  }
  std::printf("    9 kHz echo: 1x %.1f dB, 0.25x %.1f dB\n", Db(level[0]), Db(level[1]));
  CHECK(Db(level[0]) - Db(level[1]) > 30.0f, "slow tape should be dark");
  return true;
}

MONSOON_TEST(tape_trigger_stops_and_restarts_the_tape) {
  Module m;
  m.SetMode(MODE_TAPE);
  auto in = Music(6.0f);
  const size_t stop_block = 48000 / kBlockSize;           // 1 s
  const size_t start_block = 4 * 48000 / kBlockSize;      // 4 s
  auto out = m.Render(in, [&](size_t b) {
    Controls c = TapeControls();
    c.trigger = b == stop_block || b == start_block;
    return c;
  });
  float running = Measure(out, 24000, 48000).rms;
  float stopped = Measure(out, 3 * 48000, 4 * 48000).rms;
  float restarted = Measure(out, 5 * 48000, 6 * 48000).rms;
  std::printf("    rms running %.1f dB, stopped %.1f dB, restarted %.1f dB\n",
              Db(running), Db(stopped), Db(restarted));
  CHECK(Db(stopped) < Db(running) - 40.0f, "stopped tape should go quiet");
  CHECK(Db(restarted) > Db(running) - 6.0f, "tape should spin up again");
  return true;
}

MONSOON_TEST(tape_freeze_loops_forever) {
  Module m;
  m.SetMode(MODE_TAPE);
  auto in = Music(2.0f);
  auto s = Silence(8.0f);
  in.insert(in.end(), s.begin(), s.end());
  auto out = m.Render(in, [&](size_t b) {
    Controls c = TapeControls();
    c.position = 0.75f;  // 1+2+3
    c.freeze = b * kBlockSize > 2 * 48000 - 4800;
    return c;
  });
  float early = Measure(out, 3 * 48000, 4 * 48000).rms;
  float late = Measure(out, 9 * 48000, 10 * 48000).rms;
  std::printf("    frozen loop rms: 3-4 s %.1f dB, 9-10 s %.1f dB\n", Db(early), Db(late));
  CHECK(Db(early) > -40.0f, "loop is empty");
  CHECK(std::fabs(Db(late) - Db(early)) < 3.0f, "loop should sustain");
  return true;
}

MONSOON_TEST(tape_save_and_load_restore_the_tape) {
  Module a;
  a.SetMode(MODE_TAPE);
  a.Render(Music(2.0f), [](size_t) { return TapeControls(); });
  PersistentChunk chunks[kMaxPersistentChunks + 1];
  size_t n = a.engine().GetPersistentData(chunks);
  CHECK(n > 0, "nothing saved");
  std::vector<uint8_t> slot;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
    slot.insert(slot.end(), p, p + chunks[i].size);
    while (slot.size() & 3) slot.push_back(0);
  }
  CHECK(slot.size() <= 120 * 1024 + 64, "slot %zu bytes", slot.size());
  auto play = [](Module& m) {
    return m.Render(Silence(0.5f), [](size_t) {
      Controls c = TapeControls();
      c.position = 0.75f;
      return c;
    });
  };
  auto oa = play(a);
  Module b;
  b.engine().RequestLoad(slot.data(), slot.size());
  b.Render(Silence(0.05f), [](size_t) { return TapeControls(); });
  auto ob = play(b);
  float ra = Measure(oa, 4800).rms, rb = Measure(ob, 4800).rms;
  std::printf("    %zu bytes; replay rms original %.1f dB, reloaded %.1f dB\n",
              slot.size(), Db(ra), Db(rb));
  CHECK(Db(rb) > -40.0f, "reloaded tape is empty");
  CHECK(std::fabs(Db(ra) - Db(rb)) < 3.0f, "reloaded tape differs");
  return true;
}

MONSOON_TEST(tape_bal_fbk_rvb_do_what_they_say) {
  // A short noise burst on the left only, head 1, repeats on.
  auto in = Silence(2.0f);
  Random rng;
  rng.Seed(3);
  for (size_t i = 0; i < 480; ++i) in[4800 + i].l = 0.6f * rng.Bipolar();
  float delay = 0.0f;
  auto run = [&](float bal, float fbk, float rvb) {
    Module m;
    m.SetMode(MODE_TAPE);
    auto out = m.Render(in, [&](size_t) {
      Controls c = TapeControls();
      c.density = 0.6f;
      c.bal = bal;
      c.fbk = fbk;
      c.rvb = rvb;
      return c;
    });
    delay = static_cast<TapeMode*>(m.engine().mode_instance(MODE_TAPE))
        ->head_delay_seconds(0);
    return out;
  };
  auto neutral = run(0.5f, 0.0f, 0.0f);
  const size_t d = size_t(delay * 48000.0f);
  // The 3rd repeat (two passes through the loop filters).
  const size_t r3 = 4800 + 3 * d;
  auto hi_lo = [&](const std::vector<FloatFrame>& v) {
    std::vector<FloatFrame> w(v.begin() + r3 - 480, v.begin() + r3 + 1440);
    float lo = 0, hi = 0;
    Svf a, b;
    a.Init(); b.Init();
    a.set(300.0f / 48000.0f, 0.7f);
    b.set(4000.0f / 48000.0f, 0.7f);
    for (auto& f : w) {
      float l = a.Lp(f.l), h = b.Hp(f.l);
      lo += l * l;
      hi += h * h;
    }
    return Db(std::sqrt(hi)) - Db(std::sqrt(lo));
  };
  auto dark = run(0.0f, 0.0f, 0.0f);
  auto thin = run(1.0f, 0.0f, 0.0f);
  std::printf("    3rd repeat, >4 kHz minus <300 Hz: noon %+.1f, BAL left %+.1f,"
              " BAL right %+.1f dB\n", hi_lo(neutral), hi_lo(dark), hi_lo(thin));
  CHECK(hi_lo(neutral) - hi_lo(dark) > 15.0f, "BAL left should darken the repeats");
  CHECK(hi_lo(thin) - hi_lo(neutral) > 15.0f, "BAL right should thin the repeats");

  // FBK: ping-pong. The 2nd repeat of a left-only burst lands on the right.
  auto pong = run(0.5f, 1.0f, 0.0f);
  const size_t r2 = 4800 + 2 * d;
  float right_only[2], left_only[2];
  for (int k = 0; k < 2; ++k) {
    const auto& v = k == 0 ? neutral : pong;
    double l = 0, r = 0;
    for (size_t i = r2 - 480; i < r2 + 960; ++i) { l += v[i].l * v[i].l; r += v[i].r * v[i].r; }
    left_only[k] = float(std::sqrt(l));
    right_only[k] = float(std::sqrt(r));
  }
  std::printf("    2nd repeat right minus left: noon %+.1f dB, FBK %+.1f dB\n",
              Db(right_only[0]) - Db(left_only[0]), Db(right_only[1]) - Db(left_only[1]));
  // (Only the engine's mono bass below 60 Hz reaches the right at noon.)
  CHECK(Db(right_only[0]) - Db(left_only[0]) < -20.0f, "noon: the repeats stay left");
  CHECK(Db(right_only[1]) - Db(left_only[1]) > 20.0f, "FBK should ping-pong the repeats");

  // RVB: diffusion smears each repeat: less of the 3rd repeat's energy
  // sits in its first 15 ms.
  auto wash = run(0.5f, 0.0f, 1.0f);
  auto focus = [&](const std::vector<FloatFrame>& v) {
    double core = 0, all = 0;
    for (size_t i = r3 - 240; i < r3 + 4800; ++i) {
      double e = v[i].l * v[i].l;
      all += e;
      if (i < r3 + 480 + 240) core += e;
    }
    return float(core / (all + 1e-20));
  };
  std::printf("    3rd repeat energy in its first 15 ms: noon %.2f, RVB %.2f\n",
              focus(neutral), focus(wash));
  CHECK(focus(neutral) > 0.8f, "noon repeat is a clean copy");
  CHECK(focus(wash) < 0.5f, "RVB should smear the repeats");
  for (auto* v : { &dark, &thin, &pong, &wash }) {
    Stats s = Measure(*v);
    CHECK(s.finite && s.peak < 1.0f, "bounded");
  }
  return true;
}
