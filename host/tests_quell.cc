// QUELL behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/quell.h"

using namespace host;
using namespace monsoon;

namespace {

// Pink-ish flat noise plus an optional ringing sine at 1 kHz.
std::vector<FloatFrame> NoiseWithRing(float seconds, float ring_amp) {
  auto v = Noise(seconds, 0.1f);
  for (size_t i = 0; i < v.size(); ++i) {
    float s = ring_amp * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0);
    v[i].l += s;
    v[i].r += s;
  }
  return v;
}

Controls Settings(float density) {
  Controls c = DefaultControls();
  c.position = 0.454f;   // focus at 1 kHz
  c.size = 0.0f;         // sharp
  c.density = density;
  c.texture = 0.5f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> Run(const std::vector<FloatFrame>& in, Controls c,
                            int trigger_block = -1) {
  Module m;
  m.engine().set_mono_bass(false, true);  // measure the mode, not the output section
  m.SetMode(MODE_QUELL);
  return m.Render(in, [&](size_t b) {
    Controls k = c;
    k.trigger = static_cast<int>(b) == trigger_block;
    return k;
  });
}

}  // namespace

MONSOON_TEST(quell_cuts_a_resonance_but_leaves_flat_noise) {
  auto ringing = NoiseWithRing(3.0f, 0.3f);
  auto dry = Run(ringing, Settings(0.0f));
  auto wet = Run(ringing, Settings(1.0f));
  const size_t a = 48000 * 2, b = 48000 * 3 - 32;
  float ring_dry = ToneAmplitude(dry, 1000.0f, a, b);
  float ring_wet = ToneAmplitude(wet, 1000.0f, a, b);
  auto flat = NoiseWithRing(3.0f, 0.0f);
  float flat_dry = Measure(Run(flat, Settings(0.0f)), a, b).rms;
  float flat_wet = Measure(Run(flat, Settings(1.0f)), a, b).rms;
  std::printf("    1 kHz ring: %.1f dB -> %.1f dB (cut %.1f dB); flat noise "
              "%.1f dB -> %.1f dB\n", Db(ring_dry), Db(ring_wet),
              Db(ring_dry) - Db(ring_wet), Db(flat_dry), Db(flat_wet));
  CHECK(Db(ring_dry) - Db(ring_wet) > 9.0f, "resonance not tamed");
  CHECK(std::fabs(Db(flat_dry) - Db(flat_wet)) < 1.5f, "flat noise altered");
  return true;
}

MONSOON_TEST(quell_density_zero_is_transparent) {
  // Tones well above the lowest bin (the STFT framework clears DC, so
  // content under ~50 Hz isn't a fair test at this FFT size).
  auto in = Sine(440.0f, 2.0f, 0.3f);
  auto hi = Sine(3170.0f, 2.0f, 0.2f);
  for (size_t i = 0; i < in.size(); ++i) {
    in[i].l += hi[i].l;
    in[i].r += hi[i].r;
  }
  auto out = Run(in, Settings(0.0f));
  // Compare against the input delayed by the STFT latency.
  const size_t lat = 1024 + 256;  // FFT + hop at the native size
  double err = 0, ref = 0;
  for (size_t i = 48000; i < in.size(); ++i) {
    double d = out[i].l - in[i - lat].l;
    err += d * d;
    ref += in[i - lat].l * in[i - lat].l;
  }
  float snr = 10 * std::log10(ref / (err + 1e-30));
  std::printf("    density 0 vs delayed input: %.1f dB SNR\n", snr);
  // The engine's wet-path DC blocker adds a tiny phase shift (~-45 dB of
  // "error" at 440 Hz); anything above 40 dB means QUELL itself is clean.
  CHECK(snr > 40.0f, "not transparent");
  return true;
}

MONSOON_TEST(quell_delta_plays_what_is_removed) {
  auto ringing = NoiseWithRing(3.0f, 0.3f);
  auto delta = Run(ringing, Settings(1.0f), 10);
  const size_t a = 48000 * 2, b = 48000 * 3 - 32;
  float ring = ToneAmplitude(delta, 1000.0f, a, b);
  float all = Measure(delta, a, b).rms;
  auto flat_delta = Run(NoiseWithRing(3.0f, 0.0f), Settings(1.0f), 10);
  float flat = Measure(flat_delta, a, b).rms;
  std::printf("    delta: 1 kHz %.1f dB, total %.1f dB; on flat noise %.1f dB\n",
              Db(ring), Db(all), Db(flat));
  CHECK(Db(ring) > -20.0f, "delta should contain the removed ring");
  CHECK(Db(flat) < Db(all) - 12.0f, "delta on flat noise should be quiet");
  return true;
}

MONSOON_TEST(quell_freeze_holds_the_cut_curve) {
  float held[2];
  for (int frozen = 0; frozen < 2; ++frozen) {
    Module m;
    m.engine().set_mono_bass(false, true);  // measure the mode, not the output section
    m.SetMode(MODE_QUELL);
    QuellMode* q = static_cast<QuellMode*>(m.engine().mode_instance(MODE_QUELL));
    auto in = NoiseWithRing(4.0f, 0.3f);
    for (size_t i = 48000 * 2; i < in.size(); ++i) {  // the ring stops at 2 s
      float s = 0.3f * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0);
      in[i].l -= s;
      in[i].r -= s;
    }
    m.Render(in, [&](size_t b) {
      Controls c = Settings(1.0f);
      c.freeze = frozen && b > 2900;
      return c;
    });
    held[frozen] = q->cut_db(1000.0f);
  }
  std::printf("    cut at 1 kHz 2 s after the ring stopped: %.1f dB free, "
              "%.1f dB frozen\n", held[0], held[1]);
  CHECK(held[1] > 9.0f && held[0] < 3.0f, "freeze");
  return true;
}

MONSOON_TEST(quell_trim_spans_plus_minus_12_db) {
  auto in = Sine(500.0f, 1.0f, 0.1f);
  Controls c = Settings(0.0f);
  c.pitch = 48.0f;
  float up = Measure(Run(in, c), 24000, 48000 - 32).rms;
  c.pitch = -48.0f;
  float down = Measure(Run(in, c), 24000, 48000 - 32).rms;
  float ref = 0.1f / std::sqrt(2.0f);
  std::printf("    trim: %+.1f dB / %+.1f dB\n", Db(up) - Db(ref), Db(down) - Db(ref));
  CHECK(std::fabs(Db(up) - Db(ref) - 12.0f) < 0.5f, "+12");
  CHECK(std::fabs(Db(down) - Db(ref) + 12.0f) < 0.5f, "-12");
  return true;
}

MONSOON_TEST(quell_bal_fbk_rvb_do_what_they_say) {
  const size_t a = 48000 * 2, b = 48000 * 3 - 32;

  // BAL: a ring only in the sides (L = +s, R = -s) over noise. Side focus
  // (right) cuts it; mid focus (left) leaves it alone.
  auto side_ring = Noise(3.0f, 0.1f);
  for (size_t i = 0; i < side_ring.size(); ++i) {
    float s = 0.3f * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0);
    side_ring[i].l += s;
    side_ring[i].r -= s;
  }
  Controls c = Settings(1.0f);
  float ring[3];
  for (int k = 0; k < 3; ++k) {
    c.bal = 0.5f * k;
    ring[k] = ToneAmplitude(Run(side_ring, c), 1000.0f, a, b);
  }
  std::printf("    BAL side ring: mid focus %.1f dB, linked %.1f dB, side focus "
              "%.1f dB\n", Db(ring[0]), Db(ring[1]), Db(ring[2]));
  CHECK(ring[0] > 0.25f && ring[2] < 0.25f * ring[0] && ring[1] < 0.5f * ring[0],
        "bal");

  // FBK: flat noise has no peaks, so v1.0 leaves it; the hard threshold
  // squashes it.
  auto flat = NoiseWithRing(3.0f, 0.0f);
  c = Settings(1.0f);
  float in_rms = Measure(flat, a, b).rms;
  float soft = Measure(Run(flat, c), a, b).rms;
  c.fbk = 1.0f;
  float hard = Measure(Run(flat, c), a, b).rms;
  std::printf("    FBK flat noise %.1f dB: fbk 0 %.1f dB, fbk 1 %.1f dB\n",
              Db(in_rms), Db(soft), Db(hard));
  CHECK(Db(in_rms) - Db(soft) < 2.0f && Db(soft) - Db(hard) > 8.0f, "fbk");

  // RVB: a ring that stops at 2 s. With the halo it rings on, fading.
  auto stop = NoiseWithRing(5.0f, 0.3f);
  for (size_t i = 0; i < stop.size(); ++i) {
    if (i >= 96000) stop[i].l = stop[i].r = 0.0f;
  }
  c = Settings(1.0f);
  auto dry_tail = Run(stop, c);
  c.rvb = 1.0f;
  auto halo = Run(stop, c);
  float t0 = Measure(dry_tail, 96000 + 9600, 96000 + 33600).rms;
  float t1 = Measure(halo, 96000 + 9600, 96000 + 33600).rms;
  float late = Measure(halo, 96000 + 96000, 96000 + 120000).rms;
  Stats hs = Measure(halo);
  std::printf("    RVB rms 0.2-0.7 s after the ring: %.1f dB -> %.1f dB, "
              "2-2.5 s: %.1f dB, peak %.2f\n", Db(t0), Db(t1), Db(late), hs.peak);
  CHECK(t1 > 0.01f && t1 > 20.0f * t0 && late < 0.5f * t1, "rvb tail");
  CHECK(hs.finite && hs.peak < 1.0f, "rvb level");
  return true;
}
