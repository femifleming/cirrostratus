// Tests for what a real module does that a still bench doesn't: knobs that
// are never quite still, and an audio interrupt that runs out of time.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

// Typical settings, plus the noise a module's ADC leaves after smoothing
// (~0.001 on the pots, ~0.03 semitone on PITCH) when `noise`.
Controls Typical(size_t b, bool noise) {
  Controls c = DefaultControls();
  c.position = 0.4f; c.size = 0.6f; c.density = 0.6f; c.texture = 0.6f;
  c.pitch = 7.0f; c.blend = 0.7f; c.bal = 0.6f; c.fbk = 0.4f; c.rvb = 0.5f;
  c.trigger = (b % 300) == 0;
  c.gate = (b % 300) < 150;
  if (noise) {
    const float j = (b & 1) ? 0.001f : -0.0f;
    c.position += j; c.size += j; c.density += j; c.texture += j;
    c.bal += j; c.fbk += j; c.rvb += j; c.blend += j;
    c.pitch += (b & 1) ? 0.03f : 0.0f;
  }
  return c;
}

}  // namespace

// The modes that retune from their knobs (RESONATOR, PLATES, GAMELAN) must
// not retune on ADC noise: with noisy knobs they sound exactly as with still
// ones. (On the module the retuning cost up to a third of the audio budget
// while nobody touched anything, and RESONATOR glitched.)
MONSOON_TEST(hardware_knob_noise_does_not_retune) {
  const ModeId modes[] = { MODE_RESONATOR, MODE_PLATES, MODE_GAMELAN };
  for (ModeId mode : modes) {
    auto in = Music(4.0f, 31);
    // (one Module at a time: the modes are static objects that a second
    // Module's SetMode would re-initialise under the first)
    Module still;
    CHECK(still.SetMode(mode), "init");
    // The noise is only on the knobs the mode retunes from; the rest of the
    // engine (WET, FBK, RVB, and RESONATOR's TEXTURE) follows them continuously by design, so those
    // stay still here.
    auto a = still.Render(in, [](size_t b) { return Typical(b, false); });
    Module noisy;
    CHECK(noisy.SetMode(mode), "init");
    auto out = noisy.Render(in, [](size_t b) {
      // (from 0.1 s on: PLATES' BAL glides for its first few blocks and
      // retunes on each of them, which is not noise)
      Controls c = Typical(b, b >= 150);
      Controls s = Typical(b, false);
      c.blend = s.blend; c.fbk = s.fbk; c.rvb = s.rvb; c.texture = s.texture;
      return c;
    });
    double diff = 0, ref = 0;
    for (size_t i = 0; i < a.size(); ++i) {
      double d = a[i].l - out[i].l;
      diff += d * d;
      ref += a[i].l * a[i].l;
    }
    std::printf("    %-10s noisy vs still knobs: %.1f dB apart\n",
                Engine::mode_name(mode), 10.0 * std::log10(diff / ref + 1e-30));
    CHECK(diff <= ref * 1e-6, "%s retunes on knob noise", Engine::mode_name(mode));
  }
  return true;
}

// An audio interrupt that ran out of time sheds the next block (the mode is
// skipped). With every other block shed, or every block, a mode change must
// still complete, and nothing may go non-finite.
MONSOON_TEST(hardware_switch_completes_while_shedding) {
  auto in = Music(0.5f, 7);
  for (int pattern = 0; pattern < 2; ++pattern) {
    for (int mode = MODE_FIRST; mode < MODE_LAST; ++mode) {
      Module m;
      CHECK(m.SetMode(ModeId(mode)), "init");
      const ModeId next = ModeId(mode + 1 < MODE_LAST ? mode + 1 : MODE_FIRST);
      m.engine().RequestMode(next, 0);
      size_t blocks = 0;
      bool finite = true;
      for (int k = 0; k < 40 && (m.engine().switching() || blocks == 0); ++k) {
        auto out = m.Render(in, [&](size_t b) {
          m.engine().set_shed(pattern == 1 || (b & 1));
          ++blocks;
          return Typical(b, true);
        });
        finite = finite && Measure(out).finite;
      }
      m.engine().set_shed(false);
      CHECK(finite, "%s: non-finite while shedding", Engine::mode_name(ModeId(mode)));
      CHECK(!m.engine().switching(), "%s -> %s: switch never completed while shedding (%s)",
            Engine::mode_name(ModeId(mode)), Engine::mode_name(next),
            pattern ? "every block" : "every other block");
      CHECK(m.engine().mode() == next, "wrong mode after switch");
    }
  }
  std::printf("    every mode switched away while every block / every other block was shed\n");
  return true;
}

// A minute in every mode at the heavy settings with noisy knobs and
// triggers: finite, never clipping, never stuck at a rail.
MONSOON_TEST(hardware_minute_in_every_mode) {
  auto in = Music(60.0f, 99);
  for (int mode = MODE_FIRST; mode < MODE_LAST; ++mode) {
    Module m;
    CHECK(m.SetMode(ModeId(mode)), "init");
    auto out = m.Render(in, [](size_t b) {
      Controls c = Typical(b, true);
      c.density = 1.0f; c.size = 0.95f; c.texture = 0.9f; c.fbk = 0.9f;
      c.rvb = 0.9f; c.pitch = -12.0f + ((b & 1) ? 0.03f : 0.0f);
      c.freeze = (b / 900) % 4 == 3;
      return c;
    });
    Stats s = Measure(out);
    // a sound stuck at a rail: many samples in a row within 0.1 % of full scale
    size_t run = 0, longest = 0;
    for (const auto& f : out) {
      run = std::fabs(f.l) > 0.999f ? run + 1 : 0;
      if (run > longest) longest = run;
    }
    CHECK(s.finite, "%s: NaN/inf", Engine::mode_name(ModeId(mode)));
    CHECK(s.peak <= 1.0f, "%s: clip", Engine::mode_name(ModeId(mode)));
    CHECK(longest < 480, "%s: stuck at full scale for %zu samples",
          Engine::mode_name(ModeId(mode)), longest);
  }
  std::printf("    %d modes x 60 s heavy settings, noisy knobs, freezes: ok\n", int(MODE_LAST - MODE_FIRST));
  return true;
}


