#include "typhoon/settings.h"

#include <stm32f4xx_conf.h>

#include "stmlib/system/storage.h"

namespace monsoon {

// Settings sector: sector 1 (0x08004000), shared with the stock firmware's
// settings. The bootloader stays in sector 0, the app starts at sector 2.
static stmlib::Storage<1> storage;

// The CV jacks (flipped, offset-removed channels): a stock offset is only
// taken if every one of them looks like a real 0 V reading.
static bool PlausibleOffsets(const float* offset) {
  static const uint8_t kCvChannels[] = {
    ADC_POSITION_CV, ADC_DENSITY_CV, ADC_SIZE_CV, ADC_FREQ_CV,
    ADC_STEREO_CV, ADC_FEEDBACK_CV, ADC_REVERB_CV, ADC_WET_CV,
    ADC_TEXTURE_CV
  };
  for (size_t i = 0; i < sizeof(kCvChannels); ++i) {
    float o = offset[kCvChannels[i]];
    if (!(o > 0.45f && o < 0.55f)) return false;
  }
  return true;
}

void Settings::Init() {
  freshly_baked_ = false;
  imported_calibration_ = false;
  if (storage.ParsimoniousLoad(&data_, &version_token_) &&
      UpgradeSettingsData(&data_)) {
    return;
  }
  memset(&data_, 0, sizeof(data_));
  CalibrationData& c = data_.calibration_data;
  c.pitch_offset = 66.67f;
  c.pitch_scale = -84.26f;
  for (size_t i = 0; i < ADC_CHANNEL_LAST; ++i) {
    c.offset[i] = 0.5f;  // the CV stages' +1.65 V bias, mid-scale
  }
  // A Typhoon coming from SuperParasites: keep its V/OCT calibration (and
  // its 0 V offsets, same channel order, if they look sane: they are
  // garbage if its AUX noise was on while it calibrated).
  StockSettingsData stock;
  uint16_t stock_token;
  if (storage.ParsimoniousLoad(&stock, &stock_token) &&
      stock.pitch_scale < -40.0f && stock.pitch_scale > -140.0f &&
      stock.pitch_offset > 0.0f && stock.pitch_offset < 200.0f) {
    c.pitch_offset = stock.pitch_offset;
    c.pitch_scale = stock.pitch_scale;
    if (PlausibleOffsets(stock.offset)) {
      for (size_t i = 0; i < ADC_CHANNEL_LAST; ++i) {
        c.offset[i] = stock.offset[i];
      }
    }
    imported_calibration_ = true;
  } else {
    freshly_baked_ = true;
  }
  data_.magic = kSettingsMagic;
  data_.version = kSettingsVersion;
  data_.mode = MODE_FIRST;
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
  // Sectors 8-11 (bank 1 of the F427, same numbering as the F405).
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
