// CONVOLVE mode behaviour.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/convolve.h"

using namespace host;
using namespace monsoon;

namespace {

Controls ConvNeutral() {
  Controls c = DefaultControls();
  c.position = 0.0f;
  c.size = 1.0f;       // whole capture
  c.density = 0.5f;    // envelope as captured
  c.texture = 0.5f;    // flat
  c.pitch = 0.0f;
  return c;
}

ConvolveMode* Conv(Module& m) {
  return static_cast<ConvolveMode*>(m.engine().mode_instance(MODE_CONVOLVE));
}

// Feeds a click + a half-level echo 2400 samples later, captures with TRIG.
void CaptureEcho(Module& m) {
  std::vector<FloatFrame> in = Silence(0.5f);
  in[4800].l = in[4800].r = 0.8f;
  in[7200].l = in[7200].r = 0.4f;
  m.Render(in, [](size_t b) {
    Controls c = ConvNeutral();
    c.trigger = b * kBlockSize == 10240;  // ~3000 samples after the echo
    return c;
  });
  // Let the capture and the rebuild finish.
  m.Render(Silence(0.2f), [](size_t) { return ConvNeutral(); });
}

// Plays a single impulse, returns the output.
std::vector<FloatFrame> Ping(Module& m, Controls c) {
  std::vector<FloatFrame> in = Silence(1.0f);
  in[960].l = in[960].r = 0.5f;
  return m.Render(in, [&](size_t) { return c; });
}

bool CheckEcho(const std::vector<FloatFrame>& out, const char* what) {
  size_t first = 0;
  float peak = 0.0f;
  for (size_t i = 0; i < out.size(); ++i) {
    if (std::fabs(out[i].l) > peak) {
      peak = std::fabs(out[i].l);
      first = i;
    }
  }
  float echo = 0.0f;
  for (size_t i = first + 2390; i < first + 2410 && i < out.size(); ++i) {
    echo = std::max(echo, std::fabs(out[i].l));
  }
  float between = Measure(out, first + 200, first + 2300).peak;
  std::printf("    %s: peak %.3f at %zu, echo %.3f (ratio %.2f), floor %.4f\n",
              what, peak, first, echo, echo / peak, between);
  CHECK(peak > 0.1f, "no direct sound");
  CHECK(std::fabs(echo / peak - 0.5f) < 0.08f, "echo ratio %.2f", echo / peak);
  CHECK(between < peak * 0.02f, "smeared IR");
  return true;
}

}  // namespace

MONSOON_TEST(convolve_captured_impulse_reproduces_the_echo) {
  Module m;
  CHECK(m.SetMode(MODE_CONVOLVE), "init");
  ConvolveMode* conv = Conv(m);
  std::printf("    %zu partitions, IR up to %.0f ms\n", conv->partitions(),
              conv->partitions() * ConvolveMode::kBlock * 1000.0 / 48000);
  CaptureEcho(m);
  CHECK(conv->captures() == 1, "capture count %u", conv->captures());
  CHECK(!conv->busy(), "still building");
  auto out = Ping(m, ConvNeutral());
  return CheckEcho(out, "after capture");
}

MONSOON_TEST(convolve_freeze_rising_edge_captures) {
  Module m;
  CHECK(m.SetMode(MODE_CONVOLVE), "init");
  m.Render(Noise(0.5f), [](size_t b) {
    Controls c = ConvNeutral();
    c.freeze = b > 300;   // one rising edge, then held
    return c;
  });
  CHECK(Conv(m)->captures() == 1, "captures %u", Conv(m)->captures());
  return true;
}

MONSOON_TEST(convolve_size_sets_ir_length) {
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CONVOLVE), "init");
    Controls c = ConvNeutral();
    c.position = 1.0f;   // the synthetic room starts at the oldest end
    c.size = k == 0 ? 0.0f : 1.0f;
    m.Render(Silence(0.2f), [&](size_t) { return c; });  // let it rebuild
    auto out = Ping(m, c);
    tail[k] = Measure(out, 960 + 512 + 1200, 960 + 512 + 6000).rms;
  }
  std::printf("    tail 25-125 ms after the ping: size 0 %.1f dB, size 1 %.1f dB\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(tail[1] > tail[0] * 30.0f, "size does not change the IR length");
  return true;
}

MONSOON_TEST(convolve_texture_darkens_the_ir) {
  float hf[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CONVOLVE), "init");
    Controls c = ConvNeutral();
    c.texture = k == 0 ? 0.0f : 0.5f;
    auto in = Noise(1.5f, 0.2f);
    auto out = m.Render(in, [&](size_t) { return c; });
    hf[k] = ToneAmplitude(out, 6000.0f, 24000, 72000) +
        ToneAmplitude(out, 9000.0f, 24000, 72000);
  }
  std::printf("    6+9 kHz content: dark %.5f, flat %.5f\n", hf[0], hf[1]);
  CHECK(hf[0] * 10.0f < hf[1], "texture 0 should be much darker");
  return true;
}

MONSOON_TEST(convolve_save_load_restores_the_ir) {
  std::vector<uint8_t> slot;
  {
    Module a;
    CHECK(a.SetMode(MODE_CONVOLVE), "init");
    CaptureEcho(a);
    PersistentChunk chunks[kMaxPersistentChunks + 1];
    size_t n = a.engine().GetPersistentData(chunks);
    for (size_t i = 0; i < n; ++i) {
      const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
      slot.insert(slot.end(), p, p + chunks[i].size);
      while (slot.size() & 3) slot.push_back(0);
    }
  }
  Module b;
  b.engine().RequestLoad(slot.data(), slot.size());
  b.Render(Silence(0.3f), [](size_t) { return ConvNeutral(); });
  CHECK(b.engine().mode() == MODE_CONVOLVE, "mode");
  auto out = Ping(b, ConvNeutral());
  return CheckEcho(out, "after reload");
}

MONSOON_TEST(convolve_bal_fbk_rvb_do_what_they_say) {
  // BAL: a tone that starts at 0.5 s through the starting room. Left keeps
  // only its attack, right only its body.
  auto tone = Silence(2.0f);
  for (size_t i = 24000; i < tone.size(); ++i) {
    tone[i].l = tone[i].r = 0.3f * std::sin(2.0 * M_PI * 300.0 * i / 48000.0);
  }
  float onset[3], body[3];
  const float bals[3] = { 0.0f, 0.5f, 1.0f };
  for (int k = 0; k < 3; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CONVOLVE), "init");
    auto out = m.Render(tone, [&](size_t) {
      Controls c = ConvNeutral();
      c.bal = bals[k];
      return c;
    });
    // Latency 1024 samples; the first 10 ms of the response vs 1-1.5 s.
    onset[k] = Measure(out, 24000 + 1024, 24000 + 1024 + 480).rms;
    body[k] = Measure(out, 48000, 72000).rms;
  }
  std::printf("    BAL onset/body rms: left %.4f/%.4f  noon %.4f/%.4f  right %.4f/%.4f\n",
              onset[0], body[0], onset[1], body[1], onset[2], body[2]);
  CHECK(body[0] < 0.1f * body[1], "left still passes the body");
  CHECK(onset[0] > 0.3f * onset[1], "left lost the attack");
  CHECK(onset[2] < 0.5f * onset[1], "right still passes the attack");
  CHECK(body[2] > 0.7f * body[1], "right lost the body");

  // FBK: the engine's feedback loop, so a ping rings on.
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CONVOLVE), "init");
    Controls c = ConvNeutral();
    c.fbk = k == 0 ? 0.0f : 0.7f;
    CHECK(m.engine().mode_instance(MODE_CONVOLVE)->feedback_amount(c) == c.fbk,
          "FBK not mapped to the feedback loop");
    auto out = Ping(m, c);
    tail[k] = Measure(out, 24000, 48000).rms;  // 0.5-1 s after the ping
  }
  std::printf("    FBK: tail 0.5-1 s after a ping: %.1f dB -> %.1f dB\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(tail[1] > 30.0f * tail[0], "FBK does not extend the ring");

  // RVB: the captured click + echo IR gets echoes of itself in between.
  float floor_ratio[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    CHECK(m.SetMode(MODE_CONVOLVE), "init");
    CaptureEcho(m);
    Controls c = ConvNeutral();
    c.rvb = k == 0 ? 0.0f : 1.0f;
    m.Render(Silence(0.5f), [&](size_t) { return c; });  // rebuild
    CHECK(!Conv(m)->busy(), "still building");
    auto out = Ping(m, c);
    size_t first = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
      if (std::fabs(out[i].l) > peak) { peak = std::fabs(out[i].l); first = i; }
    }
    floor_ratio[k] = Measure(out, first + 200, first + 2300).rms / peak;
  }
  std::printf("    RVB: energy between click and echo (rms / peak): %.4f -> %.4f\n",
              floor_ratio[0], floor_ratio[1]);
  CHECK(floor_ratio[0] < 0.005f, "neutral IR smeared");
  CHECK(floor_ratio[1] > 0.02f, "RVB does not smear the IR");
  return true;
}
