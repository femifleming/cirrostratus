// FALTUNG behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/faltung_ir_ref.h"
#include "host/harness.h"
#include "monsoon/dsp/modes/faltung.h"
#include "monsoon/dsp/modes/faltung_irs.h"

using namespace host;
using namespace monsoon;

namespace {

Controls Neutral() {
  Controls c = DefaultControls();
  c.position = 0.0f;   // IR 0: room
  c.size = 1.0f;
  c.density = 0.0f;
  c.texture = 0.5f;
  c.pitch = 0.0f;
  return c;
}

std::vector<FloatFrame> ImpulseResponse(Controls c, float seconds,
                                        size_t at = 24000) {
  Module m;
  m.engine().set_mono_bass(false);  // measure the mode, not the output section
  m.SetMode(MODE_FALTUNG);
  std::vector<FloatFrame> in = Silence(seconds);
  in[at].l = in[at].r = 0.5f;
  return m.Render(in, [&](size_t) { return c; });
}

float Rms(const std::vector<FloatFrame>& v, size_t from, size_t to) {
  return Measure(v, from, to).rms;
}

}  // namespace

MONSOON_TEST(faltung_impulse_reproduces_the_stored_ir) {
  auto out = ImpulseResponse(Neutral(), 1.0f);
  // Latency = 2 blocks of 256.
  const size_t start = 24000 + 2 * FaltungMode::kBlock;
  double dot = 0, ee = 0, rr = 0;
  for (size_t i = 0; i < 256; ++i) {
    double e = 0.5 * kFaltungRef0[i];
    double r = out[start + i].l;
    dot += e * r; ee += e * e; rr += r * r;
  }
  double corr = dot / std::sqrt(ee * rr + 1e-30);
  double gain = std::sqrt(rr / (ee + 1e-30));
  std::printf("    first 256 samples vs reference: correlation %.5f, gain %.3f\n",
              corr, gain);
  CHECK(corr > 0.999 && std::fabs(gain - 1.0) < 0.02, "IR mismatch");
  return true;
}

MONSOON_TEST(faltung_tail_decays_and_size_shortens_it) {
  Controls c = Neutral();
  c.position = 0.2f;  // hall
  auto full = ImpulseResponse(c, 1.3f);
  c.size = 0.15f;
  auto shortened = ImpulseResponse(c, 1.3f);
  const size_t t0 = 24000 + 512;
  float early = Rms(full, t0 + 2400, t0 + 4800);       // 50-100 ms
  float late = Rms(full, t0 + 6000, t0 + 8000);        // 125-167 ms
  float after = Rms(full, t0 + 9000, t0 + 14000);      // past the IR
  float short_late = Rms(shortened, t0 + 2400, t0 + 4800);
  std::printf("    hall: 50-100 ms %.1f dB, 125-167 ms %.1f dB, after the IR "
              "%.1f dB; SIZE 0.15 at 50-100 ms %.1f dB\n",
              Db(early), Db(late), Db(after), Db(short_late));
  CHECK(late < early, "tail should decay");
  CHECK(Db(after) < Db(late) - 30.0f, "output should end with the IR");
  CHECK(Db(short_late) < Db(early) - 20.0f, "SIZE should shorten the IR");
  return true;
}

MONSOON_TEST(faltung_trig_and_position_change_the_ir) {
  Module m;
  m.engine().set_mono_bass(false);  // measure the mode, not the output section
  CHECK(m.SetMode(MODE_FALTUNG), "init");
  FaltungMode* mode = static_cast<FaltungMode*>(
      m.engine().mode_instance(MODE_FALTUNG));
  std::vector<FloatFrame> sil = Silence(0.1f);
  Controls c = Neutral();
  m.Render(sil, [&](size_t) { return c; });
  int a = mode->current_ir();
  m.Render(sil, [&](size_t b) { Controls k = c; k.trigger = b == 2; return k; });
  int b = mode->current_ir();
  c.position = 0.99f;
  m.Render(sil, [&](size_t) { return c; });
  int d = mode->current_ir();
  std::printf("    IR: %d (%s) -> TRIG -> %d (%s); POSITION max -> %d (%s)\n",
              a, kFaltungIrs[a].name, b, kFaltungIrs[b].name, d,
              kFaltungIrs[d].name);
  CHECK(a == 0 && b == 1, "TRIG should step to the next IR");
  CHECK(d == 0, "POSITION 7 + TRIG offset 1 wraps to IR 0");

  // Different IRs really sound different.
  Controls p = Neutral();
  auto r0 = ImpulseResponse(p, 0.8f);
  p.position = 4.5f / 8.0f;  // cabinet
  auto r4 = ImpulseResponse(p, 0.8f);
  double dot = 0, e0 = 0, e4 = 0;
  for (size_t i = 24512; i < 24512 + 4096; ++i) {
    dot += r0[i].l * r4[i].l; e0 += r0[i].l * r0[i].l; e4 += r4[i].l * r4[i].l;
  }
  double corr = dot / std::sqrt(e0 * e4 + 1e-30);
  std::printf("    room vs cabinet impulse responses: correlation %.3f\n", corr);
  CHECK(std::fabs(corr) < 0.5, "IRs too similar");
  return true;
}

MONSOON_TEST(faltung_predelay_moves_the_arrival) {
  Controls c = Neutral();
  c.density = 0.5f;  // ~100 ms
  // The pre-delay glides to its target: let it settle first.
  Module m;
  m.engine().set_mono_bass(false);  // measure the mode, not the output section
  m.SetMode(MODE_FALTUNG);
  std::vector<FloatFrame> in = Silence(1.5f);
  const size_t at = 48000;
  in[at].l = in[at].r = 0.5f;
  auto out = m.Render(in, [&](size_t) { return c; });
  float peak = 0.0f;
  for (size_t i = at; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > peak) peak = std::fabs(out[i].l);
  }
  size_t first = at;
  for (size_t i = at; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > 0.3f * peak) { first = i; break; }
  }
  float ms = (first - at - 512) / 48.0f;
  std::printf("    first arrival %.1f ms after the impulse (expected ~100 ms)\n", ms);
  CHECK(std::fabs(ms - 100.0f) < 5.0f, "pre-delay");
  return true;
}

MONSOON_TEST(faltung_freeze_sustains_and_stays_bounded) {
  Module m;
  m.engine().set_mono_bass(false);  // measure the mode, not the output section
  CHECK(m.SetMode(MODE_FALTUNG), "init");
  auto in = Music(8.0f);
  for (size_t i = 48000 * 2; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
  Controls c = Neutral();
  c.position = 0.3f;  // plate
  auto out = m.Render(in, [&](size_t b) {
    Controls k = c;
    k.freeze = b > 2900;  // just before the input stops
    return k;
  });
  float at3 = Rms(out, 48000 * 3, 48000 * 4);
  float at7 = Rms(out, 48000 * 7, 48000 * 8);
  Stats s = Measure(out);
  std::printf("    frozen: 3-4 s %.1f dB, 7-8 s %.1f dB, peak %.1f dB\n",
              Db(at3), Db(at7), Db(s.peak));
  CHECK(Db(at3) > -40.0f && Db(at7) > -40.0f, "freeze lost the sound");
  CHECK(std::fabs(Db(at7) - Db(at3)) < 6.0f, "frozen level drifts");
  CHECK(s.finite && s.peak <= 1.0f, "unbounded");
  return true;
}

MONSOON_TEST(faltung_bal_fbk_rvb_do_what_they_say) {
  // BAL: early (first 20 ms) vs late (100-170 ms) energy of the hall IR.
  const size_t t0 = 24000 + 512;
  float early_late[3], total[3];
  for (int k = 0; k < 3; ++k) {
    Controls c = Neutral();
    c.position = 0.2f;  // hall
    c.bal = 0.5f * k;
    auto ir = ImpulseResponse(c, 1.0f);
    float e = Rms(ir, t0, t0 + 960), l = Rms(ir, t0 + 4800, t0 + 8000);
    early_late[k] = Db(e) - Db(l);
    total[k] = Db(Rms(ir, t0, t0 + 8192));
  }
  std::printf("    BAL early-late: left %.1f dB, noon %.1f dB, right %.1f dB; "
              "level %.1f / %.1f / %.1f dB\n", early_late[0], early_late[1],
              early_late[2], total[0], total[1], total[2]);
  CHECK(early_late[0] > early_late[1] + 15.0f, "left keeps the early part");
  CHECK(early_late[2] < early_late[1] - 15.0f, "right keeps the tail");
  CHECK(std::fabs(total[0] - total[1]) < 4.0f && std::fabs(total[2] - total[1]) < 4.0f,
        "level kept");

  // FBK: the 170 ms room rings on through itself.
  float tail[2], late = 0.0f;
  for (int k = 0; k < 2; ++k) {
    Controls c = Neutral();
    c.position = 0.2f;
    c.fbk = k ? 1.0f : 0.0f;
    auto ir = ImpulseResponse(c, 6.0f);
    tail[k] = Rms(ir, t0 + 24000, t0 + 48000);  // 0.5-1 s
    if (k) late = Rms(ir, t0 + 48000 * 4, t0 + 48000 * 5);
  }
  std::printf("    FBK after a click: 0.5-1 s %.1f dB -> %.1f dB; 4-5 s %.1f dB "
              "(the IR itself: %.1f dB)\n", Db(tail[0]), Db(tail[1]), Db(late),
              total[1]);
  CHECK(tail[0] < 1e-6f && Db(tail[1]) > total[1] - 30.0f, "feedback tail");
  CHECK(Db(late) < Db(tail[1]) - 30.0f, "feedback tail decays");
  // ... bounded with music, and it dies away once the input stops (the
  // loop gain stays below 1), at full and at 0.7, BAL fully right.
  for (int ir = 0; ir < kNumFaltungIrs; ++ir) {
    for (int k = 0; k < 2; ++k) {
      Module m;
      m.SetMode(MODE_FALTUNG);
      auto in = Music(8.0f, 3 + ir);
      for (size_t i = 48000 * 3; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
      auto out = m.Render(in, [&](size_t) {
        Controls c = Neutral();
        c.position = (ir + 0.5f) / kNumFaltungIrs;
        c.fbk = k ? 0.7f : 1.0f;
        c.bal = 1.0f;
        return c;
      });
      Stats s = Measure(out);
      float end = Rms(out, 48000 * 7, 48000 * 8);
      CHECK(s.finite && s.peak <= 1.0f, "ir %d: peak %.2f", ir, s.peak);
      CHECK(Db(end) < -60.0f, "ir %d: 4-5 s after the input %.1f dB", ir, Db(end));
    }
  }

  // RVB: the engine's FDN carries the room on past its 170 ms.
  float late_tail[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_FALTUNG);
    auto in = Music(3.0f);
    for (size_t i = 24000; i < in.size(); ++i) in[i].l = in[i].r = 0.0f;
    auto out = m.Render(in, [&](size_t) {
      Controls c = Neutral();
      c.rvb = k ? 0.8f : 0.0f;
      return c;
    });
    late_tail[k] = Rms(out, 24000 + 48000, 24000 + 96000);  // 1-2 s
  }
  std::printf("    RVB 1-2 s after the music stops: %.1f dB -> %.1f dB\n", Db(late_tail[0]),
              Db(late_tail[1]));
  CHECK(late_tail[0] < 1e-6f && late_tail[1] > 1e-4f, "reverb");
  return true;
}
