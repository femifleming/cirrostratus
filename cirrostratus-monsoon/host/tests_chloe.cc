// CHLOE (Chloaelis strip) behaviour tests.

#include <cmath>
#include <cstdio>

#include <algorithm>

#include "host/harness.h"
#include "monsoon/dsp/modes/chloe.h"

using namespace host;
using namespace monsoon;

namespace {

// Finds the strongest echo of an impulse after `skip` samples.
size_t EchoArrival(const std::vector<FloatFrame>& out, size_t skip) {
  size_t best = 0;
  float peak = 0.0f;
  for (size_t i = skip; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > peak) { peak = std::fabs(out[i].l); best = i; }
  }
  return best;
}

}  // namespace

MONSOON_TEST(chloe_every_engine_echoes_at_the_size_time) {
  // SIZE s -> 20 ms * 2^(6.32 s): 20 ms .. 1.6 s. Since 1.5 the PT2399
  // clock stops at 160 kHz and the BBD's at 64 kHz (shorter lines below
  // 0.275 s / 64 ms instead): the echo must still come at the SIZE time.
  const char* names[3] = { "PT2399", "BBD", "tape" };
  const float sizes[4] = { 0.0f, 0.2f, 0.5f, 0.8f };
  for (int engine = 0; engine < 3; ++engine) {
    std::printf("    %-6s", names[engine]);
    for (float size : sizes) {
      const float expected = 0.02f * exp2f(size * 6.32f) * 48000.0f;
      // Tape glides to a new time like real tape (from noon, where the
      // module started): check it at noon only.
      if (engine == 2 && size != 0.5f) continue;
      Module m;
      CHECK(m.SetMode(MODE_CHLOE, engine), "init");
      std::vector<FloatFrame> in = Silence(1.0f + expected / 48000.0f);
      for (int i = 0; i < 96; ++i) in[4800 + i].l = in[4800 + i].r = 0.5f * std::sin(i * 0.3f);
      auto out = m.Render(in, [size](size_t) {
        Controls c = DefaultControls();
        c.size = size; c.density = 0.0f; c.position = 0.95f; c.texture = 0.0f;
        return c;
      });
      size_t arrival = EchoArrival(out, 4800 + 96 + size_t(expected / 3.0f)) - 4800;
      std::printf("  %6.1f ms (%6.1f)", arrival / 48.0, expected / 48.0);
      CHECK(std::fabs(float(arrival) - expected) < 0.06f * expected + 200.0f,
            "%s echo at %.1f ms, expected %.1f", names[engine], arrival / 48.0,
            expected / 48.0);
    }
    std::printf("\n");
  }
  return true;
}

MONSOON_TEST(chloe_short_delays_cost_no_more_than_the_cap) {
  // The PT2399 / BBD lines never clock faster than their caps, so a short
  // delay costs the CPU what 0.275 s (PT2399) / 64 ms (BBD) does.
  CHECK(chloe::Pt2399Line::kMaxRate <= 160000.0f, "PT2399 cap");
  CHECK(chloe::BbdLine::kMaxClk <= 64000.0f, "BBD cap");
  CHECK(chloe::Pt2399Line::kBits / chloe::Pt2399Line::kMaxRate > 0.27f, "PT2399: the cap "
        "only touches delays shorter than SIZE ~60 %%");
  return true;
}

MONSOON_TEST(chloe_string_engine_plays_the_tuned_note) {
  for (float pitch : { -12.0f, 0.0f, 7.0f }) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, 3), "init");
    std::vector<FloatFrame> in = Silence(1.5f);
    auto out = m.Render(in, [&](size_t b) {
      Controls c = DefaultControls();
      c.pitch = pitch; c.size = 0.0f; c.density = 0.95f; c.position = 1.0f;
      c.texture = 0.0f; c.trigger = (b == 10);
      return c;
    });
    float hz = 130.81f * std::pow(2.0f, pitch / 12.0f);
    float at = ToneAmplitude(out, hz, 24000, 60000);
    float off = ToneAmplitude(out, hz * 1.06f, 24000, 60000);
    std::printf("    pitch %+5.1f: %.1f Hz amplitude %.4f, a semitone up %.4f\n",
                pitch, hz, at, off);
    CHECK(at > 4.0f * off && at > 0.002f, "string not at the note");
  }
  return true;
}

MONSOON_TEST(chloe_freeze_holds_the_delay) {
  Module m;
  CHECK(m.SetMode(MODE_CHLOE, 0), "init");
  auto in = Music(4.0f);
  for (size_t i = 48000 * 2; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
  auto out = m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.size = 0.45f; c.density = 0.3f; c.texture = 0.2f; c.position = 0.9f;
    c.freeze = b > 2900;  // just before the input stops
    return c;
  });
  float late = Measure(out, 48000 * 3, 48000 * 4).rms;
  std::printf("    frozen PT2399 loop, 1-2 s after the input stopped: %.1f dBFS\n", Db(late));
  CHECK(Db(late) > -40.0f, "freeze lost the loop");
  return true;
}

MONSOON_TEST(chloe_stays_bounded_at_full_regen_and_drive) {
  for (int option = 0; option < 8; ++option) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, option), "init");
    auto in = Music(6.0f, option);
    auto out = m.Render(in, [](size_t b) {
      Controls c = DefaultControls();
      c.density = 1.0f; c.texture = 1.0f; c.size = (b % 400) / 400.0f;
      c.position = 0.7f; c.fbk = 0.8f; c.trigger = (b % 500) == 0;
      return c;
    });
    Stats s = Measure(out);
    CHECK(s.finite && s.peak <= 1.0f, "option %d unbounded", option);
  }
  return true;
}

MONSOON_TEST(chloe_bal_fbk_rvb_do_what_they_say) {
  // BAL: how far the attractor moves the left cutoff (semitones from the
  // POSITION cutoff) over 20 s, and how far it travels (speed x depth).
  const float bals[3] = { 0.0f, 0.5f, 1.0f };
  float range[3];
  float travel[3];
  for (int k = 0; k < 3; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, 0), "init");
    auto* ch = static_cast<ChloeMode*>(m.engine().mode_instance(MODE_CHLOE));
    const float base = 30.0f * exp2f(0.5f * 9.06f);
    float lo = 1e9f, hi = -1e9f, prev = 0.0f;
    travel[k] = 0.0f;
    m.Render(Silence(20.0f), [&](size_t b) {
      float st = 12.0f * std::log2(ch->cutoff_hz(0) / base);
      if (b > 1500) {
        lo = std::min(lo, st);
        hi = std::max(hi, st);
        travel[k] += std::fabs(st - prev);
      }
      prev = st;
      Controls c = DefaultControls();
      c.position = 0.5f;
      c.bal = bals[k];
      return c;
    });
    range[k] = hi - lo;
    std::printf("    BAL %.1f: left cutoff spans %.1f semitones, travels %.0f\n",
                bals[k], range[k], travel[k]);
  }
  CHECK(range[0] < 0.5f, "left: still");
  CHECK(range[1] > 6.0f && range[2] > 2.5f * range[1], "right: deeper");
  CHECK(travel[2] > 8.0f * travel[1], "right: faster and deeper");

  // FBK: crossed. A click on the left only reaches the right output
  // through the master feedback (the two strips are hard left/right).
  float right[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, 0), "init");
    std::vector<FloatFrame> in = Silence(1.5f);
    for (int i = 0; i < 96; ++i) in[4800 + i].l = 0.5f * std::sin(i * 0.3f);
    auto out = m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.size = 0.4f; c.density = 0.3f; c.position = 0.9f; c.texture = 0.2f;
      c.fbk = k ? 0.8f : 0.0f;
      return c;
    });
    double e = 0;
    for (auto& f : out) e += f.r * f.r;
    right[k] = float(std::sqrt(e / out.size()));
  }
  std::printf("    FBK right output from a left click: %.1f dB -> %.1f dB\n",
              Db(right[0]), Db(right[1]));
  CHECK(right[1] > 10.0f * right[0] && right[1] < 0.1f, "cross feedback");

  // RVB: diffusion smears the repeats: their crest factor drops.
  float crest[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CHLOE, 1), "init");  // BBD
    std::vector<FloatFrame> in = Silence(6.0f);
    for (int i = 0; i < 48; ++i) in[4800 + i].l = in[4800 + i].r = 0.6f * std::sin(i * 0.5f);
    auto out = m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.size = 0.4f; c.density = 0.6f; c.position = 0.95f; c.texture = 0.0f;
      c.rvb = k ? 1.0f : 0.0f;
      return c;
    });
    Stats s = Measure(out, 4800 + 24000, 4800 + 72000);  // repeats 2..
    crest[k] = s.peak / (s.rms + 1e-9f);
    float late = Measure(out, 48000 * 5, 48000 * 6).rms;
    std::printf("    RVB %d: repeats rms %.1f dB, crest %.1f; 5-6 s %.1f dB\n", k,
                Db(s.rms), crest[k], Db(late));
    CHECK(late < 0.3f * s.rms, "repeats die away");
  }
  CHECK(crest[1] < 0.6f * crest[0], "diffusion");
  return true;
}
