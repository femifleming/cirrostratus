// HALL mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Base() {
  Controls c = DefaultControls();
  c.position = 0.0f;
  c.size = 0.8f;
  c.density = 0.6f;
  c.texture = 0.6f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Clicked(float seconds, size_t at = 4800) {
  auto in = Silence(seconds);
  for (int i = 0; i < 32; ++i) in[at + i].l = in[at + i].r = 0.8f;
  return in;
}

std::vector<FloatFrame> Run(const std::vector<FloatFrame>& in,
                            std::function<void(size_t, Controls*)> f) {
  Module m;
  m.SetMode(MODE_HALL);
  return m.Render(in, [&](size_t b) {
    Controls c = Base();
    f(b, &c);
    return c;
  });
}

// Decay rate in dB per second between two windows.
float DecayDbPerSecond(const std::vector<FloatFrame>& v, float t0, float t1) {
  float a = Db(Measure(v, size_t(t0 * 48000), size_t((t0 + 0.5f) * 48000)).rms);
  float b = Db(Measure(v, size_t(t1 * 48000), size_t((t1 + 0.5f) * 48000)).rms);
  return (a - b) / (t1 - t0);
}

}  // namespace

MONSOON_TEST(hall_decay_follows_density_up_to_30s) {
  auto in = Clicked(8.0f);
  auto short_tail = Run(in, [](size_t, Controls* c) { c->density = 0.3f; });
  auto long_tail = Run(in, [](size_t, Controls* c) { c->density = 1.0f; });
  float d_short = DecayDbPerSecond(short_tail, 0.5f, 1.5f);
  float d_long = DecayDbPerSecond(long_tail, 2.0f, 7.0f);
  std::printf("    density 0.3: %.1f dB/s (T60 ~%.1f s); density 1: %.2f dB/s (T60 ~%.1f s)\n",
              d_short, 60.0f / d_short, d_long, 60.0f / d_long);
  CHECK(Measure(long_tail).finite, "finite");
  CHECK(60.0f / d_short < 3.0f, "short decay too long");
  CHECK(60.0f / d_long > 18.0f, "long decay should approach 30 s");
  return true;
}

MONSOON_TEST(hall_pre_delay_moves_the_onset) {
  size_t onset[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(Clicked(1.0f), [&](size_t, Controls* c) {
      c->position = k == 0 ? 0.0f : 1.0f;
    });
    onset[k] = 0;
    for (size_t i = 4800; i < out.size(); ++i) {
      if (std::fabs(out[i].l) > 0.005f || std::fabs(out[i].r) > 0.005f) {
        onset[k] = i - 4800;
        break;
      }
    }
  }
  float diff_ms = (float(onset[1]) - float(onset[0])) / 48.0f;
  std::printf("    onset: pre-delay 0 at %.1f ms, max at %.1f ms (difference %.1f ms)\n",
              onset[0] / 48.0f, onset[1] / 48.0f, diff_ms);
  CHECK(std::fabs(diff_ms - 250.0f) < 5.0f, "pre-delay should add 250 ms");
  return true;
}

MONSOON_TEST(hall_freeze_holds_and_trig_clears) {
  auto in = Music(1.0f);
  auto s = Silence(6.0f);
  in.insert(in.end(), s.begin(), s.end());
  const size_t trig_block = size_t(6.0f * 48000) / kBlockSize;
  auto out = Run(in, [&](size_t b, Controls* c) {
    c->freeze = b * kBlockSize > 44000;
    c->trigger = b == trig_block;
  });
  float a = Measure(out, 2 * 48000, 3 * 48000).rms;
  float b = Measure(out, 5 * 48000, 6 * 48000 - 4800).rms;
  float after = Measure(out, 6 * 48000 + 4800, out.size()).rms;
  std::printf("    frozen: 2-3 s %.1f dB, 5-6 s %.1f dB; after TRIG %.1f dB\n",
              Db(a), Db(b), Db(after));
  CHECK(Db(a) > -40.0f, "nothing captured");
  CHECK(std::fabs(Db(a) - Db(b)) < 1.5f, "freeze should be infinite");
  CHECK(Db(after) < -80.0f, "TRIG should clear");
  return true;
}

MONSOON_TEST(hall_texture_sets_damping) {
  float hi[2], lo[2];
  for (int k = 0; k < 2; ++k) {
    auto in = Noise(0.3f);
    auto s = Silence(2.0f);
    in.insert(in.end(), s.begin(), s.end());
    auto out = Run(in, [&](size_t, Controls* c) {
      c->texture = k == 0 ? 0.0f : 1.0f;
      c->density = 0.8f;
    });
    Svf h, l;
    h.Init(); l.Init();
    h.set(8000.0f / 48000.0f, 1.0f);
    l.set(500.0f / 48000.0f, 1.0f);
    double eh = 0, el = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      float yh = h.BpNormalized(out[i].l), yl = l.BpNormalized(out[i].l);
      if (i > 48000) { eh += yh * yh; el += yl * yl; }
    }
    hi[k] = float(eh);
    lo[k] = float(el);
  }
  float dark = 10 * std::log10(hi[0] / lo[0]);
  float bright = 10 * std::log10(hi[1] / lo[1]);
  std::printf("    8 kHz vs 500 Hz in the tail: dark %+.1f dB, bright %+.1f dB\n", dark, bright);
  CHECK(bright - dark > 20.0f, "texture should brighten the tail");
  return true;
}

MONSOON_TEST(hall_stereo_is_decorrelated) {
  auto in = Noise(0.5f);
  for (auto& f : in) f.r = f.l;
  auto s = Silence(1.5f);
  in.insert(in.end(), s.begin(), s.end());
  auto out = Run(in, [](size_t, Controls*) {});
  double lr = 0, ll = 0, rr = 0;
  for (size_t i = 48000; i < out.size(); ++i) {
    lr += out[i].l * out[i].r; ll += out[i].l * out[i].l; rr += out[i].r * out[i].r;
  }
  float corr = float(lr / std::sqrt(ll * rr));
  std::printf("    tail L/R correlation: %.2f\n", corr);
  CHECK(std::fabs(corr) < 0.3f, "tail should be wide");
  return true;
}

// Per-mode knobs: BAL = bass decay ratio, FBK = pre-delay echoes, RVB =
// diffusion. Each is measured against the neutral setting.
MONSOON_TEST(hall_bal_fbk_rvb_do_what_they_say) {
  // BAL: low (120 Hz) vs high (2 kHz) energy late in the tail.
  float bass[3];
  const float bals[3] = { 0.0f, 0.5f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    auto in = Noise(0.3f);
    auto s = Silence(2.5f);
    in.insert(in.end(), s.begin(), s.end());
    auto out = Run(in, [&](size_t, Controls* c) { c->bal = bals[k]; });
    Svf h, l;
    h.Init(); l.Init();
    h.set(2000.0f / 48000.0f, 2.0f);
    l.set(120.0f / 48000.0f, 2.0f);
    double eh = 0, el = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      float yh = h.BpNormalized(out[i].l), yl = l.BpNormalized(out[i].l);
      if (i > 72000) { eh += yh * yh; el += yl * yl; }
    }
    bass[k] = 10 * std::log10(el / eh);
  }
  std::printf("    BAL: 120 Hz vs 2 kHz in the late tail: left %+.1f dB, noon %+.1f dB, right %+.1f dB\n",
              bass[0], bass[1], bass[2]);
  CHECK(bass[1] - bass[0] > 10.0f, "BAL left should shorten the bass");
  CHECK(bass[2] - bass[1] > 6.0f, "BAL right should lengthen the bass");

  // FBK: with 150 ms of pre-delay and a short hall, a click keeps coming
  // back (one echo every 150 ms, each into the hall).
  float later[2];
  int peaks[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(Clicked(2.0f), [&](size_t, Controls* c) {
      c->position = 0.6f;
      c->density = 0.0f;
      c->fbk = k ? 1.0f : 0.0f;
    });
    later[k] = Db(Measure(out, 4800 + 48000, 4800 + 72000).rms);
    // Count echo onsets: 10 ms windows whose level jumps by > 6 dB.
    peaks[k] = 0;
    float prev = -200.0f;
    for (size_t w = 4800; w + 480 < 4800 + 72000; w += 480) {
      float lv = Db(Measure(out, w, w + 480).rms);
      if (lv > -60.0f && lv > prev + 6.0f) ++peaks[k];
      prev = lv;
    }
    CHECK(Measure(out).finite && Measure(out).peak < 1.0f, "bounded");
  }
  std::printf("    FBK: 1-1.5 s after a click (pre-delay 150 ms, decay 0.4 s): off %.1f dB, "
              "full %.1f dB; onsets in 1.5 s: %d -> %d\n", later[0], later[1], peaks[0], peaks[1]);
  CHECK(later[1] > later[0] + 40.0f, "FBK should repeat the pre-delay");
  CHECK(peaks[1] >= peaks[0] + 5, "FBK echoes should arrive one by one");

  // RVB: less diffusion = a sparser, grainier tail. Normalised echo
  // density (Abel & Huang: share of samples beyond one standard deviation,
  // / 0.3173; ~1 = Gaussian-smooth) over 1024-sample windows, 30-400 ms
  // after a click.
  float ned[2];
  for (int k = 0; k < 2; ++k) {
    auto out = Run(Clicked(1.0f), [&](size_t, Controls* c) {
      c->rvb = k ? 1.0f : 0.0f;
    });
    double sum = 0;
    int windows = 0;
    for (size_t w = 4800 + 1440; w + 1024 <= 4800 + 19200; w += 1024, ++windows) {
      double e = 0;
      for (size_t i = w; i < w + 1024; ++i) e += double(out[i].l) * out[i].l;
      const double sd = std::sqrt(e / 1024);
      int over = 0;
      for (size_t i = w; i < w + 1024; ++i) over += std::fabs(out[i].l) > sd;
      sum += over / 1024.0 / 0.3173;
    }
    ned[k] = float(sum / windows);
  }
  std::printf("    RVB: echo density 30-400 ms after a click: off %.2f, full %.2f\n",
              ned[0], ned[1]);
  CHECK(ned[1] < ned[0] - 0.2f, "RVB should make the tail grainy");

  // Everything at maximum: the tail must still decay, never grow.
  {
    auto in = Noise(0.3f);
    auto s = Silence(10.0f);
    in.insert(in.end(), s.begin(), s.end());
    auto out = Run(in, [](size_t, Controls* c) {
      c->density = 1.0f; c->texture = 1.0f; c->size = 1.0f;
      c->bal = 1.0f; c->fbk = 1.0f; c->rvb = 1.0f;
    });
    float a = Db(Measure(out, 48000, 96000).rms);
    float z = Db(Measure(out, out.size() - 48000).rms);
    std::printf("    all max: 1-2 s %.1f dB, last second %.1f dB\n", a, z);
    CHECK(z < a - 3.0f, "the tail must decay");
  }

  // Random fast CV on all three, long decay: bounded.
  Random r;
  r.Seed(3);
  auto out = Run(Music(4.0f), [&](size_t, Controls* c) {
    c->density = 1.0f;
    c->bal = r.Float(); c->fbk = r.Float(); c->rvb = r.Float();
  });
  Stats st = Measure(out);
  std::printf("    random CV: peak %.2f, rms %.1f dB\n", st.peak, Db(st.rms));
  CHECK(st.finite && st.peak < 1.5f, "random CV should stay bounded");
  return true;
}
