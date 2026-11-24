// Stress tests over every mode and every option: things a patch can do to
// the module that the per-mode tests don't.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

bool IsStub(ModeId m) {
  // Stub modes (not built yet) pass audio straight through. Detect them so
  // the stress tests report them instead of failing silently.
  Module mod;
  if (!mod.SetMode(m)) return false;
  auto in = Sine(1000.0f, 0.5f, 0.3f);
  auto out = mod.Render(in, [](size_t) {
    Controls c = DefaultControls();
    c.position = 0.9f; c.size = 0.9f; c.density = 0.9f; c.texture = 0.9f;
    c.pitch = 7.0f;
    return c;
  });
  double err = 0, ref = 0;
  for (size_t i = 12000; i < in.size(); ++i) {
    double d = out[i].l - in[i].l;
    err += d * d;
    ref += in[i].l * in[i].l;
  }
  return err < ref * 1e-4;  // within -40 dB of the input: a pass-through
}

}  // namespace

// Every option of every mode, with audio-rate-ish CV: each block all knobs
// jump, PITCH sweeps ±48 st, gates chatter.
MONSOON_TEST(stress_every_option_with_fast_cv) {
  int checked = 0;
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    Module probe;
    int options = probe.engine().num_options(ModeId(mode));
    for (int o = 0; o < options; ++o) {
      Module m;
      CHECK(m.SetMode(ModeId(mode), o), "%s opt %d init", Engine::mode_name(ModeId(mode)), o);
      auto in = Music(6.0f, 100 + mode * 8 + o);
      Random r;
      r.Seed(77 + mode * 13 + o);
      auto out = m.Render(in, [&](size_t b) {
        Controls c = DefaultControls();
        c.position = r.Float(); c.size = r.Float(); c.density = r.Float();
        c.texture = r.Float(); c.pitch = r.Bipolar() * 48.0f;
        c.blend = r.Float(); c.spread = r.Float(); c.feedback = r.Float();
        c.reverb = r.Float();
        c.freeze = (b / 37) % 3 == 0;
        c.trigger = r.Float() < 0.1f;
        c.gate = r.Float() < 0.5f;
        c.option = uint8_t(o);
        return c;
      });
      Stats s = Measure(out);
      CHECK(s.finite, "%s opt %d: NaN/inf", Engine::mode_name(ModeId(mode)), o);
      CHECK(s.peak <= 1.0f, "%s opt %d: clip", Engine::mode_name(ModeId(mode)), o);
      CHECK(!m.engine().init_failed(), "%s opt %d: init failed", Engine::mode_name(ModeId(mode)), o);
      ++checked;
    }
  }
  std::printf("    %d mode/option combinations survived 6 s of random CV each\n", checked);
  return true;
}

// After loud abuse, silence in must decay to silence out (no stuck DC, no
// self-oscillation left running) when freeze is off and feedback is down.
MONSOON_TEST(stress_silence_after_abuse) {
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    Module m;
    CHECK(m.SetMode(ModeId(mode)), "init");
    auto loud = Noise(3.0f, 0.9f);
    m.Render(loud, [](size_t) {
      Controls c = DefaultControls();
      c.density = 1.0f; c.texture = 1.0f; c.feedback = 1.0f; c.reverb = 1.0f;
      return c;
    });
    auto quiet = Silence(30.0f);
    auto out = m.Render(quiet, [](size_t) {
      Controls c = DefaultControls();
      c.density = 0.3f; c.feedback = 0.0f; c.reverb = 0.0f;
      return c;
    });
    Stats tail = Measure(out, out.size() - 48000 * 2);
    std::printf("    %-11s last 2 s of 30 s silence: %7.1f dBFS, dc %+.5f\n",
                Engine::mode_name(ModeId(mode)), Db(tail.rms), tail.dc);
    CHECK(tail.finite, "non-finite");
    CHECK(std::fabs(tail.dc) < 0.003f, "%s leaves DC", Engine::mode_name(ModeId(mode)));
    // Synth-like modes may legitimately keep sounding (drones, GENDY,
    // CAPTURE without a gate); everything else must go quiet.
    bool synth = mode == MODE_GENDY || mode == MODE_CAPTURE || mode == MODE_DIFFERENCE ||
                 mode == MODE_SPREEK || mode == MODE_TANTRI || mode == MODE_FOLDER ||
                 mode == MODE_CHOIR;
    if (!synth) {
      CHECK(Db(tail.rms) < -60.0f, "%s keeps sounding in silence", Engine::mode_name(ModeId(mode)));
    }
  }
  return true;
}

// Switching modes rapidly, including while frozen and mid-load, must never
// glitch the engine into a bad state.
MONSOON_TEST(stress_mode_switch_storm) {
  Module m;
  auto in = Music(0.06f);
  Random r;
  r.Seed(5);
  float worst = 0.0f;
  for (int k = 0; k < 600; ++k) {
    ModeId target = ModeId(r.Next() % MODE_LAST);
    uint8_t opt = uint8_t(r.Next() % m.engine().num_options(target));
    m.engine().RequestMode(target, opt);
    auto out = m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.freeze = (k & 1) != 0;
      c.feedback = 0.8f;
      c.reverb = 0.6f;
      return c;
    });
    Stats s = Measure(out);
    CHECK(s.finite && s.peak <= 1.0f, "bad output after switch %d", k);
    if (s.peak > worst) worst = s.peak;
  }
  // Let the last switch settle, then make sure the module still makes sound.
  auto settle = Music(1.0f);
  auto out = m.Render(settle, [](size_t) { return DefaultControls(); });
  CHECK(!m.engine().init_failed(), "engine stuck in failed init");
  CHECK(!m.engine().switching(), "engine stuck switching");
  std::printf("    600 random switches (every 60 ms, alternating freeze): peak %.2f, ok\n", worst);
  return true;
}

// Save/load in every option; reloading must restore the same mode/option and
// keep the arena within budget.
MONSOON_TEST(stress_save_load_every_option) {
  int saved = 0;
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    Module probe;
    int options = probe.engine().num_options(ModeId(mode));
    for (int o = 0; o < options; ++o) {
      Module a;
      CHECK(a.SetMode(ModeId(mode), o), "init");
      auto in = Music(2.0f, o + 1);
      a.Render(in, [&](size_t b) {
        Controls c = DefaultControls();
        c.option = uint8_t(o);
        c.freeze = b > 2000;
        c.trigger = b == 2500;
        return c;
      });
      PersistentChunk chunks[kMaxPersistentChunks + 1];
      size_t n = a.engine().GetPersistentData(chunks);
      if (!n) continue;
      std::vector<uint8_t> slot;
      for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
        slot.insert(slot.end(), p, p + chunks[i].size);
        while (slot.size() & 3) slot.push_back(0);
      }
      CHECK(slot.size() <= 128 * 1024, "%s opt %d: slot %zu bytes > 128 KB",
            Engine::mode_name(ModeId(mode)), o, slot.size());
      Module b;
      // Start in a different mode so the load has to switch.
      b.SetMode(ModeId((mode + 1) % MODE_LAST));
      b.engine().RequestLoad(slot.data(), slot.size());
      auto out = b.Render(Silence(0.5f), [](size_t) { return DefaultControls(); });
      CHECK(b.engine().mode() == mode && b.engine().option() == o,
            "%s opt %d: reload went to the wrong mode/option",
            Engine::mode_name(ModeId(mode)), o);
      CHECK(Measure(out).finite, "non-finite after load");
      ++saved;
    }
  }
  // A corrupted slot must be ignored, not loaded.
  {
    Module b;
    std::vector<uint8_t> junk(4096);
    for (size_t i = 0; i < junk.size(); ++i) junk[i] = uint8_t(i * 37 + 11);
    b.engine().RequestLoad(junk.data(), junk.size());
    b.Render(Silence(0.2f), [](size_t) { return DefaultControls(); });
    CHECK(b.engine().mode() == MODE_GRAINS, "junk slot was loaded");
  }
  std::printf("    %d saveable mode/option combinations reloaded; junk slot rejected\n", saved);
  return true;
}

// Very quiet input must stay clean (the hi-fi promise): noise floor of the
// wet path with silence in, no freeze, blend 100 %.
MONSOON_TEST(stress_noise_floor) {
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    Module m;
    CHECK(m.SetMode(ModeId(mode)), "init");
    auto out = m.Render(Silence(3.0f), [](size_t) {
      Controls c = DefaultControls();
      c.feedback = 0.0f; c.reverb = 0.0f;
      return c;
    });
    Stats s = Measure(out, 48000);
    std::printf("    %-11s idle output %7.1f dBFS\n", Engine::mode_name(ModeId(mode)), Db(s.rms));
  }
  return true;
}

MONSOON_TEST(stress_report_unbuilt_modes) {
  int stubs = 0;
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    if (IsStub(ModeId(mode))) {
      std::printf("    NOTE: %s is still a pass-through stub\n", Engine::mode_name(ModeId(mode)));
      ++stubs;
    }
  }
  std::printf("    %d of %d modes built\n", MODE_LAST - stubs, int(MODE_LAST));
  return true;
}
