// Layout of the persistent settings block, and its upgrade from older
// firmware versions. Portable (no STM32 headers) so the host tests can
// check that settings written by v1.0 still load.

#ifndef MONSOON_SETTINGS_DATA_H_
#define MONSOON_SETTINGS_DATA_H_

#include "stmlib/stmlib.h"

#include "monsoon/drivers/adc.h"
#include "monsoon/dsp/engine.h"

namespace monsoon {

struct CalibrationData {
  float pitch_offset;
  float pitch_scale;
  float offset[ADC_CHANNEL_LAST];
};

const int kNumSlots = 4;
const size_t kSlotBytes = 0x20000;  // one 128 KB flash sector each

struct SettingsData {
  CalibrationData calibration_data;
  uint32_t magic;
  uint8_t mode;
  uint8_t display;
  uint8_t option[MODE_LAST];
  int8_t env[MODE_LAST][ENV_TARGET_LAST];  // -2..+2 (half steps)
  // These used to be padding (always 0 in v1.0 settings), so the struct
  // keeps its size and v1.0 settings load unchanged.
  uint8_t mono_bass_off;  // 0 = bass below 60 Hz summed to mono (default)
  uint8_t version;        // 0 = written by v1.0, 1 = v1.1
  uint8_t buffer_quality; // v1.2, was padding: 0 = 48 kHz 16-bit (mode.h)
};

const uint8_t kSettingsVersion = 1;
const uint32_t kSettingsMagic = 0x4D484631;  // "MHF1"

// The stored block must keep its size, or ParsimoniousLoad (which checks a
// checksum over sizeof(SettingsData)) would not find v1.0 settings.
static_assert(sizeof(SettingsData) == 220, "settings layout changed");

// Brings a loaded block up to date. Returns false if it isn't ours.
inline bool UpgradeSettingsData(SettingsData* d) {
  if (d->magic != kSettingsMagic) return false;
  if (d->mode >= MODE_LAST) d->mode = 0;
  for (int m = 0; m < MODE_LAST; ++m) {
    for (int i = 0; i < ENV_TARGET_LAST; ++i) {
      int8_t e = d->env[m][i];
      if (e < -2 || e > 2) d->env[m][i] = 0;
    }
  }
  if (d->buffer_quality >= kNumBufferQualities) d->buffer_quality = 0;
  if (d->version < kSettingsVersion) {
    // v1.0 settings: these bytes were padding. Mono bass starts on; the
    // envelope routings keep their knobs (BAL/FBK/RVB are now per-mode).
    d->mono_bass_off = 0;
    d->version = kSettingsVersion;
  }
  return true;
}

inline EnvRouting SettingsEnvRouting(const SettingsData& d, int mode) {
  EnvRouting r;
  for (int i = 0; i < ENV_TARGET_LAST; ++i) {
    r.amount[i] = 0.5f * d.env[mode][i];
  }
  return r;
}


}  // namespace monsoon

#endif  // MONSOON_SETTINGS_DATA_H_
