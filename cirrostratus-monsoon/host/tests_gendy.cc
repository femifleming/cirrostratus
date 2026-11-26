// GENDY behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/gendy.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.rvb = 0.0f;
  return c;
}

// Normalised autocorrelation of the left channel at integer lag.
float Autocorr(const std::vector<FloatFrame>& v, size_t from, size_t to, size_t lag) {
  double xy = 0, xx = 0, yy = 0;
  for (size_t i = from; i + lag < to; ++i) {
    xy += v[i].l * v[i + lag].l;
    xx += v[i].l * v[i].l;
    yy += v[i + lag].l * v[i + lag].l;
  }
  return float(xy / std::sqrt(xx * yy + 1e-30));
}

float BestAutocorr(const std::vector<FloatFrame>& v, size_t from, size_t to,
                   size_t lo, size_t hi, size_t* best_lag = NULL) {
  float best = -1.0f;
  for (size_t lag = lo; lag <= hi; ++lag) {
    float a = Autocorr(v, from, to, lag);
    if (a > best) {
      best = a;
      if (best_lag) *best_lag = lag;
    }
  }
  return best;
}

GendyMode* Gendy(Module& m) {
  return static_cast<GendyMode*>(m.engine().mode_instance(MODE_GENDY));
}

}  // namespace

MONSOON_TEST(gendy_pitch_follows_voct) {
  const float pitches[] = { 0.0f, 12.0f, 24.0f };
  for (float p : pitches) {
    Module m;
    m.SetMode(MODE_GENDY);
    auto in = Silence(1.0f);
    auto out = m.Render(in, [&](size_t) {
      Controls c = Base();
      c.pitch = p;
      c.texture = 0.0f;  // static waveform
      c.density = 0.3f;
      return c;
    });
    float period = 48000.0f / (65.41f * std::pow(2.0f, p / 12.0f));
    size_t lag;
    float a = BestAutocorr(out, 24000, 48000 - 64,
                           size_t(period * 0.8f), size_t(period * 1.2f), &lag);
    std::printf("    pitch %+4.0f: expected period %.1f, found %zu (corr %.3f)\n",
                p, period, lag, a);
    CHECK(a > 0.95f, "not periodic");
    CHECK(std::fabs(lag - period) <= period * 0.005f, "wrong period");
  }
  return true;
}

MONSOON_TEST(gendy_freeze_stabilises_waveform) {
  float corr[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_GENDY);
    auto in = Silence(2.0f);
    auto out = m.Render(in, [&](size_t b) {
      Controls c = Base();
      c.pitch = 12.0f;
      c.texture = 1.0f;
      c.size = 1.0f;
      c.density = 0.4f;
      c.freeze = k == 1 && b > 48000 / 32;
      return c;
    });
    float period = 48000.0f / 130.81f;
    corr[k] = BestAutocorr(out, 48000 + 4800, 2 * 48000 - 64,
                           size_t(period * 0.3f), size_t(period * 2.5f));
  }
  std::printf("    best autocorrelation: %.3f wandering, %.3f frozen\n", corr[0], corr[1]);
  CHECK(corr[1] > 0.98f, "frozen waveform should repeat exactly");
  CHECK(corr[0] < corr[1], "wandering waveform should be less periodic");
  return true;
}

MONSOON_TEST(gendy_texture_makes_it_wander) {
  float wander[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_GENDY);
    auto in = Silence(0.1f);
    m.Render(in, [](size_t) { return Base(); });
    float before[GendyMode::kMaxBreakpoints];
    for (int i = 0; i < GendyMode::kMaxBreakpoints; ++i) {
      before[i] = Gendy(m)->breakpoint_amplitude(0, i);
    }
    auto in2 = Silence(0.5f);
    m.Render(in2, [&](size_t) {
      Controls c = Base();
      c.texture = k == 0 ? 0.0f : 0.8f;
      return c;
    });
    float d = 0;
    for (int i = 0; i < GendyMode::kMaxBreakpoints; ++i) {
      d += std::fabs(Gendy(m)->breakpoint_amplitude(0, i) - before[i]);
    }
    wander[k] = d;
  }
  std::printf("    breakpoint drift: %.4f (texture 0), %.4f (texture 0.8)\n",
              wander[0], wander[1]);
  CHECK(wander[0] < 1e-6f, "texture 0 must be static");
  CHECK(wander[1] > 0.5f, "texture should make the breakpoints walk");
  return true;
}

MONSOON_TEST(gendy_every_distribution_stays_bounded) {
  for (int z = 0; z <= 6; ++z) {
    Module m;
    m.SetMode(MODE_GENDY);
    auto in = Silence(1.0f);
    auto out = m.Render(in, [&](size_t) {
      Controls c = Base();
      c.position = z / 6.0f;
      c.texture = 1.0f;
      c.size = 1.0f;
      c.density = 1.0f;
      c.pitch = 30.0f;
      return c;
    });
    Stats s = Measure(out);
    CHECK(s.finite && s.peak <= 1.0f && Db(s.rms) > -40.0f,
          "distribution %d: peak %.2f rms %.1f", z, s.peak, Db(s.rms));
    for (int i = 0; i < Gendy(m)->num_breakpoints(); ++i) {
      CHECK(std::fabs(Gendy(m)->breakpoint_amplitude(0, i)) <= 1.0f, "barrier");
    }
  }
  return true;
}

MONSOON_TEST(gendy_trig_reseeds_from_input) {
  Module m;
  m.SetMode(MODE_GENDY);
  // A square wave at the voice's own pitch: reseeded breakpoints should
  // become (close to) +/-1.
  auto in = Silence(0.5f);
  float period = 48000.0f / 65.41f;
  for (size_t i = 0; i < in.size(); ++i) {
    float ph = std::fmod(i / period, 1.0f);
    in[i].l = in[i].r = ph < 0.5f ? 0.4f : -0.4f;
  }
  m.Render(in, [](size_t b) {
    Controls c = Base();
    c.density = 0.2f;
    c.texture = 0.0f;  // no walking after the reseed
    c.trigger = b == 600;
    return c;
  });
  int k = Gendy(m)->num_breakpoints();
  int extreme = 0;
  for (int i = 0; i < k; ++i) {
    if (std::fabs(Gendy(m)->breakpoint_amplitude(0, i)) > 0.99f) ++extreme;
  }
  std::printf("    %d of %d breakpoints took the square wave's +/-1\n", extreme, k);
  CHECK(extreme >= k - 1, "reseed should copy the input shape");
  return true;
}

MONSOON_TEST(gendy_save_load_restores_breakpoints) {
  std::vector<uint8_t> slot;
  std::vector<float> saved;
  {
    Module a;
    a.SetMode(MODE_GENDY);
    auto in = Silence(1.0f);
    a.Render(in, [](size_t b) {
      Controls c = Base();
      c.texture = 1.0f;
      c.freeze = b > 1000;
      return c;
    });
    PersistentChunk chunks[kMaxPersistentChunks + 1];
    size_t n = a.engine().GetPersistentData(chunks);
    CHECK(n == 2, "header + state");
    for (size_t i = 0; i < n; ++i) {
      const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
      slot.insert(slot.end(), p, p + chunks[i].size);
      while (slot.size() & 3) slot.push_back(0);
    }
    for (int i = 0; i < GendyMode::kMaxBreakpoints; ++i) {
      saved.push_back(Gendy(a)->breakpoint_amplitude(0, i));
      saved.push_back(Gendy(a)->breakpoint_amplitude(1, i));
    }
  }
  Module b;
  b.SetMode(MODE_HALL);
  b.engine().RequestLoad(slot.data(), slot.size());
  auto sil = Silence(0.3f);
  b.Render(sil, [](size_t) {
    Controls c = Base();
    c.freeze = true;
    return c;
  });
  CHECK(b.engine().mode() == MODE_GENDY, "mode switched");
  for (int i = 0; i < GendyMode::kMaxBreakpoints; ++i) {
    CHECK(Gendy(b)->breakpoint_amplitude(0, i) == saved[2 * i], "voice 0 bp %d", i);
    CHECK(Gendy(b)->breakpoint_amplitude(1, i) == saved[2 * i + 1], "voice 1 bp %d", i);
  }
  std::printf("    %zu-byte slot restored all breakpoints\n", slot.size());
  return true;
}

// Frequency of the strongest component in [lo, hi] (Goertzel scan).
static float PeakHz(const std::vector<FloatFrame>& v, float lo, float hi, bool left) {
  float best = lo, best_a = -1.0f;
  for (float hz = lo; hz <= hi; hz += 0.1f) {
    float a = ToneAmplitude(v, hz, 24000, 96000, left);
    if (a > best_a) { best_a = a; best = hz; }
  }
  return best;
}

MONSOON_TEST(gendy_bal_fbk_rvb_do_what_they_say) {
  // Static walks (TEXTURE 0): each voice is a fixed periodic waveform.
  auto run = [](float bal, float fbk, float rvb) {
    Module m;
    m.SetMode(MODE_GENDY);
    auto in = Silence(2.0f);
    return m.Render(in, [&](size_t) {
      Controls c = Base();
      c.texture = 0.0f;
      c.density = 0.2f;
      c.pitch = 12.0f;   // ~131 Hz
      c.bal = bal;
      c.fbk = fbk;
      c.rvb = rvb;
      return c;
    });
  };
  // BAL: interval of the right voice against the left one.
  float ratio[4];
  const float bals[4] = { 0.5f, 1.0f, 0.0f, 0.8f };  // 0, +12, -12, +7 st
  const float expect[4] = { 1.0f, 2.0f, 0.5f, 1.4983f };
  for (int k = 0; k < 4; ++k) {
    auto out = run(bals[k], 0.0f, 0.0f);
    float fl = PeakHz(out, 120.0f, 140.0f, true);
    float fr = PeakHz(out, 120.0f * expect[k], 140.0f * expect[k], false);
    ratio[k] = fr / fl;
    CHECK(fl > 125.0f && fl < 136.0f, "left voice moved: %.1f Hz", fl);
  }
  std::printf("    right/left pitch: noon %.3f, right %.3f, left %.3f, 0.8 %.3f\n",
              ratio[0], ratio[1], ratio[2], ratio[3]);
  for (int k = 0; k < 4; ++k) {
    CHECK(std::fabs(ratio[k] / expect[k] - 1.0f) < 0.02f,
          "BAL %.1f: interval %.3f, want %.3f", bals[k], ratio[k], expect[k]);
  }
  // FBK: the voices steer each other, so the left voice stops repeating.
  auto plain = run(0.8f, 0.0f, 0.0f);
  auto coupled = run(0.8f, 1.0f, 0.0f);
  float per_plain = BestAutocorr(plain, 24000, 72000, 355, 375);
  float per_coupled = BestAutocorr(coupled, 24000, 72000, 355, 375);
  std::printf("    left-voice periodicity: %.3f uncoupled, %.3f with FBK\n",
              per_plain, per_coupled);
  CHECK(per_plain > 0.95f, "uncoupled static walk repeats");
  CHECK(per_coupled < 0.8f, "FBK should cross-couple the voices");
  // RVB = FDN: the voices are the same with or without it (it does not
  // touch the walks), so wet - dry is the reverb itself.
  auto up = run(1.0f, 0.0f, 0.0f);
  auto wet = run(1.0f, 0.0f, 0.8f);
  std::vector<FloatFrame> tail(up.size());
  for (size_t i = 0; i < up.size(); ++i) {
    tail[i].l = wet[i].l - up[i].l;
    tail[i].r = wet[i].r - up[i].r;
  }
  float dry_rms = Measure(up, 48000).rms, rev_rms = Measure(tail, 48000).rms;
  std::printf("    RVB 0.8 adds a reverb at %.1f dB re the dry voices (%.1f dB)\n",
              Db(rev_rms) - Db(dry_rms), Db(dry_rms));
  CHECK(Db(rev_rms) - Db(dry_rms) > -12.0f, "RVB should add the reverb");
  for (auto* v : { &up, &coupled, &wet }) {
    Stats s = Measure(*v);
    CHECK(s.finite && s.peak < 1.0f, "bounded");
  }
  return true;
}
