// Persistent settings (calibration, current mode, per-mode options and
// envelope routing) and the four 128 KB save slots, Typhoon build. Same
// scheme as the Monsoon: settings in flash sector 1 (where the stock
// firmware keeps its own), slots in sectors 8-11 (0x08080000-0x080FFFFF,
// also where the stock firmware keeps them), application in sectors 2-7.

#ifndef TYPHOON_SETTINGS_H_
#define TYPHOON_SETTINGS_H_

#include "stmlib/stmlib.h"

#include "typhoon/settings_data.h"

namespace monsoon {

class Settings {
 public:
  Settings() { }
  ~Settings() { }

  void Init();
  void Save();

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

  EnvRouting env_routing(int mode) const {
    return SettingsEnvRouting(data_, mode);
  }
  bool mono_bass() const { return data_.mono_bass_off == 0; }
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

#endif  // TYPHOON_SETTINGS_H_
