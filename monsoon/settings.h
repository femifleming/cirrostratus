// Persistent settings (calibration, current mode, per-mode options and
// envelope routing) and the four 128 KB save slots.

#ifndef MONSOON_SETTINGS_H_
#define MONSOON_SETTINGS_H_

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
  uint8_t padding[3];
};

class Settings {
 public:
  Settings() { }
  ~Settings() { }

  void Init();
  void Save();

  // Save slots live in flash sectors 8-11 (0x08080000-0x080FFFFF), the same
  // place stock Clouds/Monsoon keeps them.
  inline const uint8_t* slot_data(int index) const {
    return reinterpret_cast<const uint8_t*>(0x08080000 + index * kSlotBytes);
  }
  // Writes chunks (each padded to 4 bytes) into a slot. Takes ~1-2 s.
  void WriteSlot(int index, const PersistentChunk* chunks, size_t n);

  CalibrationData* mutable_calibration_data() {
    return &data_.calibration_data;
  }
  SettingsData* mutable_data() { return &data_; }
  const SettingsData& data() const { return data_; }

  EnvRouting env_routing(int mode) const;
  bool freshly_baked() const { return freshly_baked_; }
  bool imported_calibration() const { return imported_calibration_; }

 private:
  SettingsData data_;
  uint16_t version_token_;
  bool freshly_baked_;
  bool imported_calibration_;

  DISALLOW_COPY_AND_ASSIGN(Settings);
};

}  // namespace monsoon

#endif  // MONSOON_SETTINGS_H_
