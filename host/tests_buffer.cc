// Buffer quality (v1.2): the master setting that trades fidelity for
// length in every mode that records (GRAINS, STRETCH, CAPTURE, TAPE,
// BARBERPOLE, BLOOM).

#include <cmath>
#include <cstdio>
#include <cstring>

#include "host/harness.h"
#include "monsoon/dsp/modes/barberpole.h"
#include "monsoon/dsp/modes/bloom.h"
#include "monsoon/dsp/modes/capture.h"
#include "monsoon/dsp/modes/grains.h"
#include "monsoon/dsp/modes/stretch.h"
#include "monsoon/dsp/modes/tape.h"
#include "monsoon/dsp/sample_buffer.h"
#include "monsoon/settings_data.h"

using namespace host;
using namespace monsoon;

static const char* kQualityName[kNumBufferQualities] = {
  "48k 16-bit", "24k 16-bit", "16k 16-bit",
  "48k  8-bit", "24k  8-bit", "16k  8-bit",
};

struct BufferMode {
  ModeId id;
  uint8_t option;
  const char* label;
};

static const BufferMode kBufferModes[] = {
  { MODE_GRAINS, 0, "grains stereo" },
  { MODE_GRAINS, 4, "grains mono" },
  { MODE_STRETCH, 0, "stretch stereo" },
  { MODE_STRETCH, 1, "stretch mono" },
  { MODE_CAPTURE, 0, "capture" },
  { MODE_TAPE, 0, "tape stereo" },
  { MODE_TAPE, 1, "tape mono" },
  { MODE_BARBERPOLE, 0, "barberpole" },
  { MODE_BLOOM, 0, "bloom" },
};

// The length that matters in each mode, in seconds.
static float ModeSeconds(Module& m, ModeId id) {
  Mode* mode = m.engine().mode_instance(id);
  switch (id) {
    case MODE_GRAINS: return static_cast<GrainsMode*>(mode)->buffer_seconds();
    case MODE_STRETCH: return static_cast<StretchMode*>(mode)->buffer_seconds();
    case MODE_CAPTURE: return static_cast<CaptureMode*>(mode)->buffer_seconds();
    case MODE_TAPE: return static_cast<TapeMode*>(mode)->head3_seconds();
    case MODE_BARBERPOLE:
      return static_cast<BarberpoleMode*>(mode)->max_delay_seconds();
    case MODE_BLOOM: return static_cast<BloomMode*>(mode)->max_window_seconds();
    default: return 0.0f;
  }
}

static bool SetModeQuality(Module& m, ModeId id, uint8_t option, uint8_t q) {
  m.engine().set_buffer_quality(q);
  if (!m.SetMode(id, option)) return false;
  // Same mode already running: the quality change alone re-inits it.
  std::vector<FloatFrame> silence(kBlockSize * 64);
  for (int tries = 0; tries < 200 && m.engine().switching(); ++tries) {
    m.Run(DefaultControls(), silence.data(), NULL, kBlockSize * 4);
  }
  return !m.engine().init_failed();
}

MONSOON_TEST(buffer_quality_lengths_every_mode) {
  std::printf("    %-15s", "");
  for (int q = 0; q < kNumBufferQualities; ++q) {
    std::printf(" %11s", kQualityName[q]);
  }
  std::printf("\n");
  for (const BufferMode& b : kBufferModes) {
    float s[kNumBufferQualities];
    for (int q = 0; q < kNumBufferQualities; ++q) {
      Module m;
      CHECK(SetModeQuality(m, b.id, b.option, uint8_t(q)), "%s q%d init",
            b.label, q);
      CHECK(m.engine().mode_instance(b.id)->buffer_quality() == q,
            "%s runs q%d", b.label, q);
      s[q] = ModeSeconds(m, b.id);
    }
    std::printf("    %-15s", b.label);
    for (int q = 0; q < kNumBufferQualities; ++q) std::printf(" %10.2fs", s[q]);
    std::printf("\n");
    for (int q = 1; q < kNumBufferQualities; ++q) {
      // At least 90% of the ideal factor (block rounding, guards).
      float want = s[0] * QualityLengthFactor(uint8_t(q)) * 0.9f;
      CHECK(s[q] >= want, "%s q%d: %.2f s, want >= %.2f s", b.label, q,
            s[q], want);
    }
  }
  return true;
}

MONSOON_TEST(buffer_quality_every_mode_plays_clean) {
  for (const BufferMode& b : kBufferModes) {
    for (int q = 1; q < kNumBufferQualities; ++q) {
      Module m;
      CHECK(SetModeQuality(m, b.id, b.option, uint8_t(q)), "init");
      // 9 s: TAPE's slowest settings put heads 2+3 up to 7 s back.
      auto in = Music(9.0f, 7);
      auto out = m.Render(in, [&](size_t blk) {
        Controls c = DefaultControls();
        c.blend = 1.0f;
        c.freeze = blk > 9000 && blk < 11000;
        c.trigger = blk == 4500 && b.id != MODE_TAPE;  // TAPE: stop
        c.gate = blk > 3000 && blk < 3400;
        return c;
      });
      float peak = 0.0f;
      double e = 0.0;
      bool finite = true;
      for (size_t i = out.size() / 4; i < out.size(); ++i) {
        if (!std::isfinite(out[i].l) || !std::isfinite(out[i].r)) finite = false;
        peak = std::fmax(peak, std::fmax(std::fabs(out[i].l), std::fabs(out[i].r)));
        e += out[i].l * out[i].l + out[i].r * out[i].r;
      }
      float rms = std::sqrt(e / (2.0 * static_cast<double>(out.size() - out.size() / 4)));
      CHECK(finite, "%s q%d: not finite", b.label, q);
      CHECK(peak < 1.5f, "%s q%d: peak %.2f", b.label, q, peak);
      CHECK(rms > 0.003f, "%s q%d: silent (rms %.4f)", b.label, q, rms);
    }
  }
  return true;
}

// A SampleBuffer on its own: write a tone, read it back at 48 kHz frames.
static void ToneThroughBuffer(uint8_t q, float hz, float amp, float* level,
                              float* alias_level, float alias_hz,
                              float* error_db) {
  std::vector<uint8_t> mem(200 * 1024);
  Arena arena;
  arena.Init(mem.data(), mem.size(), NULL, 0);
  SampleBuffer buf;
  buf.Init(&arena, 118 * 1024, 1, 0, q);
  const size_t n = 48000;
  std::vector<FloatFrame> in(n);
  for (size_t i = 0; i < n; ++i) {
    float x = amp * std::sin(2.0 * M_PI * hz * i / 48000.0);
    in[i].l = in[i].r = x;
  }
  for (size_t i = 0; i + kBlockSize <= n; i += kBlockSize) {
    buf.WriteBlock(&in[i]);
  }
  // Read back the last 0.25 s (well inside what's stored).
  const size_t len = 12000;
  std::vector<FloatFrame> out(len);
  const float start = static_cast<float>(buf.write_head()) -
      static_cast<float>(len) - static_cast<float>(buf.guard()) - 16.0f;
  for (size_t i = 0; i < len; ++i) {
    float v = buf.Read(0, start + static_cast<float>(i));
    out[i].l = out[i].r = v;
  }
  *level = ToneAmplitude(out, hz, 0, len);
  *alias_level = ToneAmplitude(out, alias_hz, 0, len);
  // Error against the ideal tone, after lining up the filter's delay:
  // residual once the fitted sine at `hz` is removed.
  double sc = 0.0, cc = 0.0, total = 0.0;
  for (size_t i = 0; i < len; ++i) {
    double ph = 2.0 * M_PI * hz * i / 48000.0;
    sc += out[i].l * std::sin(ph);
    cc += out[i].l * std::cos(ph);
  }
  sc *= 2.0 / len;
  cc *= 2.0 / len;
  for (size_t i = 0; i < len; ++i) {
    double ph = 2.0 * M_PI * hz * i / 48000.0;
    double r = out[i].l - (sc * std::sin(ph) + cc * std::cos(ph));
    total += r * r;
  }
  double sig = 0.5 * (sc * sc + cc * cc);
  *error_db = static_cast<float>(10.0 * std::log10(total / len / sig + 1e-20));
}

MONSOON_TEST(buffer_quality_passband_noise_and_aliasing) {
  for (int q = 0; q < kNumBufferQualities; ++q) {
    const int d = QualityDecimation(uint8_t(q));
    float level, alias, err;
    // 1 kHz passes at full level.
    ToneThroughBuffer(uint8_t(q), 1000.0f, 0.5f, &level, &alias, 3000.0f, &err);
    std::printf("    %s: 1 kHz %+.2f dB, noise+distortion %.1f dB",
                kQualityName[q], Db(level / 0.5f), err);
    CHECK(std::fabs(Db(level / 0.5f)) < 0.3f, "q%d passband level", q);
    // 8-bit: quantisation. 16 kHz: mostly the Hermite read between
    // samples 3 ms apart (-64 dB at 1 kHz), not the bit depth.
    CHECK(err < (QualityEightBit(uint8_t(q)) ? -35.0f : -60.0f),
          "q%d noise %.1f dB", q, err);
    if (d > 1) {
      // A tone above the stored band must not fold back down.
      const float fs = 48000.0f / d;
      const float hz = 0.62f * fs;     // e.g. 9.9 kHz at 16 kHz
      const float folded = fs - hz;    // where its alias would land
      ToneThroughBuffer(uint8_t(q), hz, 0.5f, &level, &alias, folded, &err);
      std::printf(", %.0f Hz alias at %.0f Hz: %.1f dB", hz, folded,
                  Db(alias / 0.5f));
      CHECK(Db(alias / 0.5f) < -65.0f, "q%d aliasing", q);
    }
    std::printf("\n");
  }
  return true;
}

MONSOON_TEST(buffer_quality_slot_reloads_at_its_own_quality) {
  for (const BufferMode& b : kBufferModes) {
    Module a;
    CHECK(SetModeQuality(a, b.id, b.option, 5), "init");
    auto in = Music(3.0f, 3);
    a.Render(in, [](size_t blk) {
      Controls c = DefaultControls();
      c.freeze = blk > 4000;
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
    CHECK(slot.size() <= 128 * 1024 - 64, "%s slot %zu bytes", b.label,
          slot.size());
    Module m;  // master setting left at 48k 16-bit
    m.engine().RequestLoad(slot.data(), slot.size());
    auto out = m.Render(Silence(1.0f), [](size_t) {
      Controls c = DefaultControls();
      c.freeze = true;
      c.blend = 1.0f;
      return c;
    });
    CHECK(!m.engine().init_failed(), "%s load init", b.label);
    CHECK(m.engine().mode_instance(b.id)->buffer_quality() == 5,
          "%s should reload at 16k 8-bit", b.label);
    CHECK(m.engine().buffer_quality() == 0, "master setting unchanged");
    if (b.id == MODE_GRAINS || b.id == MODE_STRETCH) {
      float e = 0.0f;
      for (size_t i = out.size() / 2; i < out.size(); ++i) e += std::fabs(out[i].l);
      CHECK(e > 1.0f, "%s frozen slot should play", b.label);
    }
    std::printf("    %-15s 16k 8-bit slot %6zu bytes reloaded\n", b.label,
                slot.size());
    // Changing the master setting puts the mode back on it.
    m.engine().set_buffer_quality(0);
    m.Render(Silence(0.3f), [](size_t) { return DefaultControls(); });
    CHECK(m.engine().mode_instance(b.id)->buffer_quality() == 0,
          "%s back to the master setting", b.label);
  }
  return true;
}

MONSOON_TEST(buffer_quality_only_reinits_buffer_modes) {
  Module m;
  CHECK(m.SetMode(MODE_HALL), "hall");
  m.engine().set_buffer_quality(4);
  CHECK(!m.engine().switching(), "hall has no buffer: no restart");
  CHECK(m.SetMode(MODE_GRAINS), "grains");
  CHECK(m.engine().mode_instance(MODE_GRAINS)->buffer_quality() == 4,
        "a buffer mode picks up the master setting when it starts");
  m.engine().set_buffer_quality(4);
  CHECK(!m.engine().switching(), "same setting: no restart");
  m.engine().set_buffer_quality(1);
  CHECK(m.engine().switching(), "new setting restarts the buffer mode");
  m.engine().set_buffer_quality(200);
  CHECK(m.engine().buffer_quality() == 0, "out of range -> 48k 16-bit");
  return true;
}

MONSOON_TEST(buffer_quality_setting_byte) {
  SettingsData d;
  memset(&d, 0, sizeof(d));
  d.magic = kSettingsMagic;
  d.version = 1;  // as v1.1 wrote it: the byte was padding (0)
  CHECK(UpgradeSettingsData(&d), "loads");
  CHECK(d.buffer_quality == 0, "v1.1 settings: 48k 16-bit");
  d.buffer_quality = 5;
  CHECK(UpgradeSettingsData(&d) && d.buffer_quality == 5, "kept");
  d.buffer_quality = 9;
  CHECK(UpgradeSettingsData(&d) && d.buffer_quality == 0, "garbage -> 0");
  CHECK(sizeof(SettingsData) == 220, "layout unchanged");
  return true;
}
