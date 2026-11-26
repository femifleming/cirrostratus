// FFT size (v1.4): the master setting for the spectral modes (SPECTRAL and
// its SMEAR option, CROSS, STRETCH, PUTZ, QUELL, SPECDELAY, MIRROR).

#include <cmath>
#include <cstdio>
#include <cstring>

#include "host/harness.h"
#include "monsoon/settings_data.h"

using namespace host;
using namespace monsoon;

struct SpectralCase {
  ModeId id;
  uint8_t option;
  const char* label;
  size_t native;
};

static const SpectralCase kSpectral[] = {
  { MODE_SPECTRAL, 0, "spectral", 2048 },
  { MODE_SPECTRAL, 1, "smear", 512 },
  { MODE_CROSS, 0, "cross", 2048 },
  { MODE_STRETCH, 0, "stretch", 2048 },
  { MODE_STRETCH, 1, "stretch mono", 2048 },
  { MODE_PUTZ, 0, "putz", 1024 },
  { MODE_QUELL, 0, "quell", 1024 },
  { MODE_SPECDELAY, 0, "specdelay", 512 },
  { MODE_MIRROR, 0, "mirror", 1024 },
};

static bool SetModeFft(Module& m, ModeId id, uint8_t option, uint8_t fft) {
  m.engine().set_fft_setting(fft);
  return m.SetMode(id, option);
}

static Controls Busy(size_t) {
  Controls c = DefaultControls();
  c.fbk = 0.3f;
  c.rvb = 0.3f;
  return c;
}

MONSOON_TEST(fft_every_spectral_mode_runs_at_every_size) {
  for (const SpectralCase& sc : kSpectral) {
    float rms_native = 0.0f;
    std::printf("    %-13s", sc.label);
    for (uint8_t s = 0; s < kNumFftSettings; ++s) {
      Module m;
      CHECK(SetModeFft(m, sc.id, sc.option, s), "%s init at setting %d",
            sc.label, s);
      const size_t top = m.engine().mode_instance(sc.id)->max_fft_size();
      const size_t want = s == 0 ? sc.native
          : (FftSettingSize(s) > top ? top : FftSettingSize(s));
      CHECK(m.engine().fft_size() == want, "%s setting %d runs at %zu, not %zu",
            sc.label, s, m.engine().fft_size(), want);
      CHECK(m.engine().mode_instance(sc.id)->fft_setting() == s,
            "%s setting %d fell back to its own size", sc.label, s);
      auto in = Music(5.0f, 7);
      auto out = m.Render(in, Busy);
      const Stats st = Measure(out, out.size() / 3);
      CHECK(st.finite, "%s at %zu: not finite", sc.label, want);
      CHECK(st.peak < 4.0f, "%s at %zu: peak %.2f", sc.label, want, st.peak);
      CHECK(st.rms > 1e-4f, "%s at %zu: silent", sc.label, want);
      if (s == 0) rms_native = st.rms;
      const float d = Db(st.rms) - Db(rms_native);
      // The same sound at another resolution: the level must not jump.
      CHECK(std::fabs(d) < 6.0f, "%s at %zu: %.1f dB from its own size",
            sc.label, want, d);
      std::printf("  %4zu %+5.1f dB", want, d);
    }
    std::printf("\n");
  }
  return true;
}

MONSOON_TEST(fft_setting_change_restarts_the_running_mode) {
  Module m;
  CHECK(SetModeFft(m, MODE_SPECTRAL, 0, 0), "init");
  CHECK(m.engine().fft_size() == 2048, "own size first");
  m.engine().set_fft_setting(1);
  m.Render(Silence(0.5f), [](size_t) { return DefaultControls(); });
  CHECK(m.engine().fft_size() == 256, "256 after the change, got %zu",
        m.engine().fft_size());
  m.engine().set_fft_setting(0);
  m.Render(Silence(0.5f), [](size_t) { return DefaultControls(); });
  CHECK(m.engine().fft_size() == 2048, "back to its own size");
  // Non-spectral modes don't notice (and report no FFT).
  CHECK(m.SetMode(MODE_GRAINS, 0), "grains");
  m.engine().set_fft_setting(3);
  CHECK(!m.engine().switching(), "grains is not restarted");
  CHECK(m.engine().fft_size() == 0, "grains has no FFT");
  return true;
}

MONSOON_TEST(fft_slot_reloads_at_its_own_size) {
  // A frozen SPECTRAL slot stores its spectrum: bin for bin, so it must come
  // back at the size it was saved with, whatever the master setting is.
  std::vector<uint8_t> slot;
  {
    Module a;
    CHECK(SetModeFft(a, MODE_SPECTRAL, 0, 2), "init at 512");
    a.Render(Music(3.0f, 3), [](size_t blk) {
      Controls c = DefaultControls();
      c.freeze = blk > 3000;
      return c;
    });
    PersistentChunk chunks[kMaxPersistentChunks + 1];
    size_t n = a.engine().GetPersistentData(chunks);
    CHECK(n > 0, "spectral saves");
    for (size_t i = 0; i < n; ++i) {
      const uint8_t* p = static_cast<const uint8_t*>(chunks[i].data);
      slot.insert(slot.end(), p, p + chunks[i].size);
      while (slot.size() & 3) slot.push_back(0);
    }
  }
  Module m;  // master setting: each mode's own size
  m.engine().RequestLoad(slot.data(), slot.size());
  auto out = m.Render(Silence(1.5f), [](size_t) {
    Controls c = DefaultControls();
    c.freeze = true;
    return c;
  });
  CHECK(!m.engine().init_failed(), "load init");
  CHECK(m.engine().fft_size() == 512, "reloaded at %zu, not 512",
        m.engine().fft_size());
  CHECK(m.engine().fft_setting() == 0, "master setting unchanged");
  const Stats st = Measure(out, out.size() / 2);
  CHECK(st.finite && st.rms > 1e-3f, "the frozen spectrum plays (rms %g)",
        st.rms);
  // A morph source of another size is refused (bins wouldn't line up).
  m.engine().set_fft_setting(4);
  m.Render(Silence(0.5f), [](size_t) { return DefaultControls(); });
  CHECK(m.engine().fft_size() == 2048, "2048 now");
  m.engine().SetMorphSource(slot.data(), slot.size());
  m.Render(Silence(0.3f), [](size_t) {
    Controls c = DefaultControls();
    c.position = 0.8f;
    return c;
  });
  return true;
}

MONSOON_TEST(fft_settings_byte_upgrades) {
  SettingsData d;
  memset(&d, 0, sizeof(d));
  d.magic = kSettingsMagic;
  CHECK(UpgradeSettingsData(&d), "ours");
  CHECK(d.fft_setting == 0, "old settings (padding 0): each mode's own size");
  d.fft_setting = 9;
  CHECK(UpgradeSettingsData(&d), "ours");
  CHECK(d.fft_setting == 0, "out of range -> own size");
  d.fft_setting = 4;
  CHECK(UpgradeSettingsData(&d) && d.fft_setting == 4, "kept");
  return true;
}
