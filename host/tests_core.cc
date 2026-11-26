// Engine-level tests that apply to every mode.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/sample_buffer.h"
#include "monsoon/dsp/stft.h"

using namespace host;
using namespace monsoon;

namespace {

class IdentityClient : public StftClient {
 public:
  void Transform(float* spectrum[2], size_t fft_size) { }
};

}  // namespace

MONSOON_TEST(stft_identity_has_unity_gain) {
  std::vector<uint8_t> mem(200000);
  Arena arena;
  arena.Init(mem.data(), mem.size(), NULL, 0);
  IdentityClient client;
  Stft stft;
  CHECK(stft.Init(&arena, 2048, 512, &client), "init");
  auto in = Sine(1000.0f, 1.0f, 0.5f);
  std::vector<FloatFrame> out(in.size());
  for (size_t i = 0; i < in.size(); i += kBlockSize) {
    stft.Process(&in[i], &out[i], kBlockSize);
    while (stft.Prepare()) { }
  }
  // Compare against the input delayed by the latency.
  size_t lat = stft.latency();
  double err = 0, ref = 0;
  for (size_t i = lat + 4096; i < in.size(); ++i) {
    double d = out[i].l - in[i - lat].l;
    err += d * d;
    ref += in[i - lat].l * in[i - lat].l;
  }
  float snr = 10 * std::log10(ref / (err + 1e-20));
  std::printf("    latency %zu samples, reconstruction SNR %.1f dB\n", lat, snr);
  CHECK(snr > 60.0f, "snr %.1f", snr);
  return true;
}

MONSOON_TEST(sample_buffer_keeps_quiet_signals_clean) {
  std::vector<uint8_t> mem(100000);
  Arena arena;
  arena.Init(mem.data(), mem.size(), NULL, 0);
  SampleBuffer buf;
  CHECK(buf.Init(&arena, 60000, 2), "init");
  auto in = Sine(440.0f, 0.3f, 0.001f);  // -60 dBFS
  for (size_t i = 0; i + kBlockSize <= in.size() && i < buf.size(); i += kBlockSize) {
    buf.WriteBlock(&in[i]);
  }
  double err = 0, ref = 0;
  for (size_t i = 0; i < buf.size() && i < in.size(); ++i) {
    double d = buf.Sample(0, i) - in[i].l;
    err += d * d;
    ref += in[i].l * in[i].l;
  }
  float snr = 10 * std::log10(ref / (err + 1e-30));
  std::printf("    -60 dBFS sine stored with SNR %.1f dB (plain int16 would be ~38 dB)\n", snr);
  CHECK(snr > 80.0f, "snr %.1f", snr);
  return true;
}

MONSOON_TEST(every_mode_initialises_within_memory) {
  Module m;
  CHECK(m.ok(), "engine init");
  for (int i = MODE_FIRST; i < MODE_LAST; ++i) {
    for (int o = 0; o < m.engine().num_options(ModeId(i)); ++o) {
      CHECK(m.SetMode(ModeId(i), o), "mode %s option %d failed to init",
            Engine::mode_name(ModeId(i)), o);
      if (o == 0) {
        std::printf("    %-11s %6zu / %zu bytes, %d option(s)\n",
                    Engine::mode_name(ModeId(i)), m.engine().arena_used(),
                    m.engine().arena_capacity(), m.engine().num_options(ModeId(i)));
      }
    }
  }
  return true;
}

MONSOON_TEST(every_mode_survives_random_knob_abuse) {
  for (int mode = MODE_FIRST; mode < MODE_LAST; ++mode) {
    Module m;
    CHECK(m.SetMode(ModeId(mode)), "init");
    auto in = Music(14.0f, mode + 3);
    Random rng;
    rng.Seed(1000 + mode);
    Controls c = DefaultControls();
    auto out = m.Render(in, [&](size_t b) {
      if (b % 300 == 0) {  // every 0.2 s jump everything somewhere new
        c.position = rng.Float();
        c.size = rng.Float();
        c.density = rng.Float();
        c.texture = rng.Float();
        c.pitch = rng.Bipolar() * 48.0f;
        c.blend = rng.Float();
        c.bal = rng.Float();
        c.fbk = rng.Float();
        c.rvb = rng.Float();
        c.option = uint8_t(rng.Next() % m.engine().num_options(ModeId(mode)));
        if (rng.Float() < 0.3f) c.freeze = !c.freeze;
      }
      c.trigger = (b % 97) == 0;
      c.gate = (b % 97) < 40;
      return c;
    });
    Stats s = Measure(out);
    std::printf("    %-11s peak %6.1f dBFS  rms %6.1f dBFS  dc %+.4f\n",
                Engine::mode_name(ModeId(mode)), Db(s.peak), Db(s.rms), s.dc);
    CHECK(s.finite, "non-finite output in %s", Engine::mode_name(ModeId(mode)));
    CHECK(s.peak <= 1.0f, "clipping in %s", Engine::mode_name(ModeId(mode)));
    CHECK(std::fabs(s.dc) < 0.02f, "dc offset in %s", Engine::mode_name(ModeId(mode)));
  }
  return true;
}

MONSOON_TEST(every_mode_makes_sound_from_music) {
  for (int mode = MODE_FIRST; mode < MODE_LAST; ++mode) {
    Module m;
    CHECK(m.SetMode(ModeId(mode)), "init");
    auto in = Music(6.0f);
    auto out = m.Render(in, [&](size_t b) {
      Controls c = DefaultControls();
      c.trigger = (b % 750) == 0;
      c.gate = (b % 750) < 300;
      return c;
    });
    Stats s = Measure(out, out.size() / 2);
    std::printf("    %-11s rms %6.1f dBFS\n", Engine::mode_name(ModeId(mode)), Db(s.rms));
    CHECK(Db(s.rms) > -45.0f, "%s is (nearly) silent", Engine::mode_name(ModeId(mode)));
  }
  return true;
}

MONSOON_TEST(blend_fully_dry_passes_input_through) {
  Module m;
  m.engine().set_mono_bass(false, true);  // the mono-bass crossover is an allpass on the highs
  auto in = Sine(997.0f, 1.0f, 0.4f);
  auto out = m.Render(in, [](size_t) {
    Controls c = DefaultControls();
    c.blend = 0.0f;
    return c;
  });
  double err = 0, ref = 0;
  for (size_t i = 4800; i < in.size(); ++i) {
    double d = out[i].l - in[i].l;
    err += d * d;
    ref += in[i].l * in[i].l;
  }
  float snr = 10 * std::log10(ref / (err + 1e-30));
  std::printf("    dry path SNR %.1f dB\n", snr);
  CHECK(snr > 60.0f, "dry path altered: %.1f dB", snr);
  return true;
}

MONSOON_TEST(mode_switches_are_click_free) {
  Module m;
  auto in = Sine(220.0f, 0.25f, 0.4f);
  float worst = 0.0f;
  for (int mode = MODE_FIRST; mode < MODE_LAST; ++mode) {
    m.engine().RequestMode(ModeId(mode), 0);
    auto out = m.Render(in, [](size_t) { return DefaultControls(); });
    for (size_t i = 1; i < out.size(); ++i) {
      float jump = std::fabs(out[i].l - out[i - 1].l);
      if (jump > worst) worst = jump;
    }
  }
  std::printf("    largest sample-to-sample step across all switches: %.3f\n", worst);
  CHECK(worst < 0.5f, "click %.3f", worst);
  return true;
}

MONSOON_TEST(reverb_tail_grows_with_slider) {
  float tails[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CAPTURE), "capture");  // RVB = the FDN reverb
    std::vector<FloatFrame> in = Silence(6.0f);
    for (int i = 0; i < 64; ++i) in[i].l = in[i].r = 0.8f;
    auto out = m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.blend = 0.0f;
      c.rvb = k == 0 ? 0.3f : 0.9f;
      return c;
    });
    tails[k] = Measure(out, 48000 * 2, 48000 * 3).rms;
    CHECK(Measure(out).finite, "finite");
  }
  std::printf("    tail rms at 2-3 s: %.1f dB (slider 0.3), %.1f dB (slider 0.9)\n",
              Db(tails[0]), Db(tails[1]));
  CHECK(tails[1] > tails[0] * 4.0f, "long reverb should ring much longer");
  return true;
}

MONSOON_TEST(save_and_load_round_trip_every_mode) {
  for (int mode = MODE_FIRST; mode < MODE_LAST; ++mode) {
    Module a;
    CHECK(a.SetMode(ModeId(mode)), "init");
    auto in = Music(3.0f, 11);
    a.Render(in, [](size_t b) {
      Controls c = DefaultControls();
      c.freeze = b > 3000;
      c.trigger = b == 3500;
      return c;
    });
    PersistentChunk chunks[kMaxPersistentChunks + 1];
    size_t n = a.engine().GetPersistentData(chunks);
    if (n == 0) {
      std::printf("    %-11s (nothing to save)\n", Engine::mode_name(ModeId(mode)));
      continue;
    }
    std::vector<uint8_t> slot;
    for (size_t i = 0; i < n; ++i) {
      const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
      slot.insert(slot.end(), p, p + chunks[i].size);
      while (slot.size() & 3) slot.push_back(0);
    }
    CHECK(slot.size() <= 128 * 1024 - 64, "%s slot too big: %zu",
          Engine::mode_name(ModeId(mode)), slot.size());
    Module b;
    b.engine().RequestLoad(slot.data(), slot.size());
    std::vector<FloatFrame> sil = Silence(0.5f);
    b.Render(sil, [](size_t) { return DefaultControls(); });
    CHECK(b.engine().mode() == mode, "load switched to the wrong mode");
    CHECK(!b.engine().init_failed(), "load init");
    std::printf("    %-11s %6zu bytes saved and reloaded\n",
                Engine::mode_name(ModeId(mode)), slot.size());
  }
  return true;
}

MONSOON_TEST(cmsis_fft_matches_shyfft_layout_and_scale) {
  for (size_t n : { 256, 512, 1024, 2048 }) {
    std::vector<float> x(n), a(n), b(n), ca(n), cb(n);
    Random r;
    r.Seed(n);
    for (auto& v : x) v = r.Bipolar();
    ca = x; cb = x;
    ShyFftDirect(ca.data(), a.data(), n);
    FftDirect(cb.data(), b.data(), n);
    double err = 0, ref = 0;
    for (size_t k = 0; k < n; ++k) {
      if (k == 0 || k == n / 2) continue;  // DC/Nyquist slots: frameworks clear them
      err += (a[k] - b[k]) * (a[k] - b[k]);
      ref += a[k] * a[k];
    }
    float snr = 10 * std::log10(ref / (err + 1e-30));
    // Inverse round trip.
    std::vector<float> y(n), s2 = b;
    FftInverse(s2.data(), y.data(), n);
    double e2 = 0, r2 = 0;
    for (size_t i = 0; i < n; ++i) {
      double d = y[i] / float(n) - x[i];
      e2 += d * d; r2 += x[i] * x[i];
    }
    float rt = 10 * std::log10(r2 / (e2 + 1e-30));
    std::printf("    n=%4zu: CMSIS vs ShyFFT agree to %.1f dB, round trip %.1f dB\n", n, snr, rt);
    CHECK(snr > 100.0f && rt > 100.0f, "mismatch");
  }
  return true;
}
