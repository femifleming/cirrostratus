// GRAINS behaviour tests.

#include <cmath>
#include <cstdio>

#include "host/harness.h"
#include "monsoon/dsp/modes/grains.h"

using namespace host;
using namespace monsoon;

static GrainsMode* Grains(Module& m) {
  return static_cast<GrainsMode*>(m.engine().mode_instance(MODE_GRAINS));
}

MONSOON_TEST(grains_scale_quantisation) {
  CHECK(GrainsMode::Quantize(6.6f, 0) == 7.0f, "chromatic");
  CHECK(GrainsMode::Quantize(1.2f, 1) == 0.0f, "pentatonic down");
  CHECK(GrainsMode::Quantize(4.4f, 1) == 5.0f, "pentatonic 5");
  CHECK(GrainsMode::Quantize(-0.8f, 1) == 0.0f, "pentatonic below 0");
  CHECK(GrainsMode::Quantize(6.0f, 2) == 7.0f, "fifth");
  CHECK(GrainsMode::Quantize(10.0f, 2) == 12.0f, "octave");
  CHECK(GrainsMode::Quantize(3.3f, 3) == 3.3f, "free");
  return true;
}

static void PitchedGrains(float pitch, int option, float* at_in,
                          float* at_target, float target_hz) {
  Module m;
  m.SetMode(MODE_GRAINS, option);
  auto in = Sine(440.0f, 3.0f, 0.5f);
  auto out = m.Render(in, [&](size_t b) {
    Controls c = DefaultControls();
    c.size = 0.0f;
    c.density = 0.6f;
    c.texture = 0.5f;
    c.pitch = pitch;
    c.option = uint8_t(option);
    c.freeze = b > 1500;
    return c;
  });
  // Grains have random phases, so their energy spreads a few Hz around the
  // note: take the strongest line within +/-8 Hz.
  size_t a = out.size() - 48000, z = out.size();
  auto band = [&](float hz) {
    float best = 0.0f;
    for (float f = hz - 8.0f; f <= hz + 8.0f; f += 1.0f) {
      float v = ToneAmplitude(out, f, a, z);
      if (v > best) best = v;
    }
    return best;
  };
  *at_in = band(440.0f);
  *at_target = band(target_hz);
}

MONSOON_TEST(grains_pitch_lands_on_the_right_note) {
  float a440, a880;
  PitchedGrains(12.0f, 0, &a440, &a880, 880.0f);
  std::printf("    +12 st: 880 Hz %.1f dB, 440 Hz %.1f dB\n", Db(a880), Db(a440));
  CHECK(a880 > a440 * 20.0f, "octave up should dominate");
  float a659;
  PitchedGrains(6.0f, 2, &a440, &a659, 659.26f);
  std::printf("    +6 st on fifths scale -> 659 Hz %.1f dB, 440 Hz %.1f dB\n",
              Db(a659), Db(a440));
  CHECK(a659 > a440 * 20.0f, "quantised to a fifth");
  return true;
}

MONSOON_TEST(grains_freeze_holds_sound_after_input_stops) {
  float level[2];
  for (int frozen = 0; frozen < 2; ++frozen) {
    Module m;
    m.SetMode(MODE_GRAINS);
    auto in = Music(2.0f);
    auto tail = Silence(4.0f);
    in.insert(in.end(), tail.begin(), tail.end());
    auto out = m.Render(in, [&](size_t b) {
      Controls c = DefaultControls();
      c.freeze = frozen && b > 2900;
      return c;
    });
    level[frozen] = Measure(out, out.size() - 48000).rms;
  }
  std::printf("    last second: %.1f dB unfrozen, %.1f dB frozen\n",
              Db(level[0]), Db(level[1]));
  CHECK(Db(level[1]) > -40.0f, "frozen buffer should keep sounding");
  CHECK(Db(level[0]) < -80.0f, "unfrozen buffer should empty out");
  return true;
}

MONSOON_TEST(grains_trigger_cycles_trajectory_shapes) {
  Module m;
  m.SetMode(MODE_GRAINS);
  GrainsMode* g = Grains(m);
  CHECK(g->shape() == GrainsMode::SHAPE_CIRCLE, "starts on circle");
  auto in = Music(1.0f);
  m.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.trigger = b == 100 || b == 200 || b == 300;
    return c;
  });
  CHECK(g->shape() == GrainsMode::SHAPE_SPIRAL, "three triggers -> spiral (%d)",
        int(g->shape()));
  return true;
}

MONSOON_TEST(grains_mono_option_doubles_the_buffer) {
  Module m;
  m.SetMode(MODE_GRAINS, 0);
  float stereo = Grains(m)->buffer_seconds();
  m.SetMode(MODE_GRAINS, 4);
  float mono = Grains(m)->buffer_seconds();
  std::printf("    buffer: %.2f s stereo, %.2f s mono\n", stereo, mono);
  CHECK(mono > stereo * 1.9f, "mono should be ~2x");
  return true;
}

MONSOON_TEST(grains_density_controls_grain_count) {
  int counts[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    m.SetMode(MODE_GRAINS);
    auto in = Music(2.0f);
    int peak = 0;
    m.Render(in, [&](size_t) {
      Controls c = DefaultControls();
      c.density = k ? 0.95f : 0.1f;
      int a = Grains(m)->active_grains();
      if (a > peak) peak = a;
      return c;
    });
    counts[k] = peak;
  }
  std::printf("    max simultaneous grains: %d (sparse), %d (dense)\n",
              counts[0], counts[1]);
  CHECK(counts[1] > counts[0], "denser should overlap more");
  return true;
}

MONSOON_TEST(grains_reloaded_slot_plays_frozen_audio) {
  Module a;
  a.SetMode(MODE_GRAINS);
  auto in = Music(2.0f);
  a.Render(in, [](size_t b) {
    Controls c = DefaultControls();
    c.freeze = b > 2800;
    return c;
  });
  PersistentChunk chunks[kMaxPersistentChunks + 1];
  size_t n = a.engine().GetPersistentData(chunks);
  CHECK(n > 0, "has data");
  std::vector<uint8_t> slot;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
    slot.insert(slot.end(), p, p + chunks[i].size);
    while (slot.size() & 3) slot.push_back(0);
  }
  Module b;
  b.engine().RequestLoad(slot.data(), slot.size());
  auto sil = Silence(3.0f);
  auto out = b.Render(sil, [](size_t) {
    Controls c = DefaultControls();
    c.freeze = true;
    return c;
  });
  float rms = Measure(out, out.size() - 48000).rms;
  std::printf("    reloaded and frozen: %.1f dB with silent input\n", Db(rms));
  CHECK(Db(rms) > -40.0f, "reloaded buffer should sound");
  return true;
}
