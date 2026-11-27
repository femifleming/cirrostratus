// Layout of the Typhoon's persistent settings block. Same fields as the
// Monsoon's (monsoon/settings_data.h) with the Typhoon's calibration (12
// ADC1 channels) and its own magic, so neither firmware ever reads the
// other's block. The SuperParasites block (76 bytes) is imported once for
// its V/OCT calibration (settings.cc).

#ifndef TYPHOON_SETTINGS_DATA_H_
#define TYPHOON_SETTINGS_DATA_H_

#include "stmlib/stmlib.h"

#include "typhoon/drivers/adc.h"
#include "monsoon/dsp/engine.h"

namespace monsoon {

struct CalibrationData {
  float pitch_offset;
  float pitch_scale;
  float offset[ADC_CHANNEL_LAST];  // raw ADC1 reading of each jack at 0 V
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
  uint8_t mono_bass_off;  // 0 = bass below 60 Hz summed to mono (default)
  uint8_t version;
  uint8_t buffer_quality; // 0 = 48 kHz 16-bit (mode.h)
  uint8_t fft_setting;    // 0 = each mode's own size
  uint8_t aux_random;     // 1 = the AUX normal carries the random voltage
  uint8_t padding[3];
};

const uint8_t kSettingsVersion = 1;
const uint32_t kSettingsMagic = 0x54484631;  // "THF1"

// Keep the block's size fixed from the first Typhoon release on: the
// loader finds the block by its size.
static_assert(sizeof(SettingsData) == 220, "settings layout changed");

// The stock SuperParasites block (supercell/settings.h): 76 bytes.
struct StockSettingsData {
  float pitch_offset;
  float pitch_scale;
  float offset[12];      // same channel order as our AdcChannel
  uint8_t quality;
  uint8_t playback_mode;
  float random_rate;
  bool random_state;
  uint8_t padding[8];
};
static_assert(sizeof(StockSettingsData) == 76, "stock layout");

// Brings a loaded block up to date. Returns false if it isn't ours.
inline bool UpgradeSettingsData(SettingsData* d) {
  if (d->magic != kSettingsMagic) return false;
  if (!ModeExists(d->mode)) d->mode = MODE_FIRST;
  for (int m = 0; m < MODE_LAST; ++m) {
    for (int i = 0; i < ENV_TARGET_LAST; ++i) {
      int8_t e = d->env[m][i];
      if (e < -2 || e > 2) d->env[m][i] = 0;
    }
  }
  if (d->buffer_quality >= kNumBufferQualities) d->buffer_quality = 0;
  if (d->fft_setting >= kNumFftSettings) d->fft_setting = 0;
  if (d->aux_random > 1) d->aux_random = 0;
  if (d->display > 1) d->display = 0;
  d->version = kSettingsVersion;
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

#endif  // TYPHOON_SETTINGS_DATA_H_
