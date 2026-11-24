#include "monsoon/settings.h"

#include <stm32f4xx_conf.h>

#include "stmlib/system/storage.h"

namespace monsoon {

// Settings sector: sector 1 (0x08004000), shared with the stock firmware's
// settings. The bootloader stays in sector 0, the app starts at sector 2.
static stmlib::Storage<1> storage;


// Layout written by the stock Monsoon (Clouds 1.x) firmware, used once to
// carry over an existing V/OCT calibration.
struct LegacySettingsData {
  CalibrationData calibration_data;
  uint8_t quality;
  uint8_t playback_mode;
  uint8_t padding_state;
  uint8_t padding[8];
};

void Settings::Init() {
  freshly_baked_ = false;
  imported_calibration_ = false;
  if (storage.ParsimoniousLoad(&data_, &version_token_) &&
      UpgradeSettingsData(&data_)) {
    return;
  }
  memset(&data_, 0, sizeof(data_));
  LegacySettingsData legacy;
  uint16_t legacy_token;
  if (storage.ParsimoniousLoad(&legacy, &legacy_token) &&
      legacy.calibration_data.pitch_scale < -40.0f &&
      legacy.calibration_data.pitch_scale > -140.0f) {
    data_.calibration_data = legacy.calibration_data;
    imported_calibration_ = true;
  } else {
    data_.calibration_data.pitch_offset = 66.67f;
    data_.calibration_data.pitch_scale = -84.26f;
    for (size_t i = 0; i < ADC_CHANNEL_LAST; ++i) {
      data_.calibration_data.offset[i] = 0.505f;
    }
    freshly_baked_ = true;
  }
  data_.magic = kSettingsMagic;
  data_.version = kSettingsVersion;
  data_.mode = MODE_GRAINS;
  version_token_ = 0;
  Save();
}

void Settings::Save() {
  storage.ParsimoniousSave(data_, &version_token_);
}

void Settings::WriteSlot(int index, const PersistentChunk* chunks, size_t n) {
  uint32_t address = 0x08080000 + index * kSlotBytes;
  const uint32_t end = address + kSlotBytes;
  FLASH_Unlock();
  FLASH_ClearFlag(
      FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
      FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
  FLASH_EraseSector((8 + index) * 8, VoltageRange_3);
  for (size_t c = 0; c < n; ++c) {
    const uint8_t* bytes = static_cast<const uint8_t*>(chunks[c].data);
    size_t size = chunks[c].size;
    for (size_t i = 0; i < size; i += 4) {
      uint32_t word = 0;
      for (size_t k = 0; k < 4 && i + k < size; ++k) {
        word |= static_cast<uint32_t>(bytes[i + k]) << (8 * k);
      }
      if (address + 4 > end) {
        FLASH_Lock();
        return;
      }
      FLASH_ProgramWord(address, word);
      address += 4;
    }
  }
  FLASH_Lock();
}

}  // namespace monsoon
