// Buttons and LEDs.
//
//   FREEZE            tap: freeze on/off
//   DISP              tap: LEDs show input level <-> WET/STER/FDB/RVB
//   DISP (hold)       mode menu: POSITION picks the mode (shown in binary,
//                     green = bank 1, modes 1-15; red = bank 2, 16-30),
//                     WRITE tap switches bank, FREEZE cycles the mode's
//                     option, release DISP to apply
//   DISP hold + WRITE hold 1 s   V/OCT calibration (as on stock firmware)
//   WRITE             tap: load page   hold: save page
//                     (DISP picks the slot, WRITE confirms; in the load page
//                      FREEZE arms/clears the slot as the morph source)
//   FREEZE hold + DISP   envelope page: DISP moves the cursor over
//                     WET/STER/FDB/RVB, FREEZE cycles off/+½/+1/−½/−1,
//                     WRITE saves and leaves

#ifndef MONSOON_UI_H_
#define MONSOON_UI_H_

#include "stmlib/stmlib.h"

#include "monsoon/drivers/leds.h"
#include "monsoon/drivers/switches.h"
#include "monsoon/dsp/engine.h"

namespace monsoon {

enum UiPage {
  UI_PAGE_SPLASH,
  UI_PAGE_VU_METER,
  UI_PAGE_KNOBS,
  UI_PAGE_MODE_MENU,
  UI_PAGE_LOAD,
  UI_PAGE_SAVE,
  UI_PAGE_SAVING,
  UI_PAGE_ENV,
  UI_PAGE_CALIBRATION_1,
  UI_PAGE_CALIBRATION_2,
  UI_PAGE_PANIC,
};

enum SwitchIndex {
  SWITCH_DISP,
  SWITCH_WRITE,
  SWITCH_FREEZE
};

class CvScaler;
class Meter;
class Settings;

class Ui {
 public:
  Ui() { }
  ~Ui() { }

  void Init(Settings* settings, CvScaler* cv_scaler, Engine* engine,
            Meter* meter);
  void Poll();      // 1 kHz, from SysTick
  void DoEvents();  // main loop
  void Panic() { page_ = UI_PAGE_PANIC; }

 private:
  void OnPress(uint8_t id);
  void OnLongPress(uint8_t id);
  void OnRelease(uint8_t id, uint32_t duration);
  void PaintLeds();
  void ShowBinary(uint8_t value, bool green, uint8_t brightness);
  void ApplyModeMenu();
  void SaveSlot();
  void SaveSettings();
  uint8_t ModeFromPosition() const;

  Settings* settings_;
  CvScaler* cv_scaler_;
  Engine* engine_;
  Meter* meter_;
  Leds leds_;
  Switches switches_;

  volatile UiPage page_;
  UiPage previous_page_;
  uint32_t press_time_[kNumSwitches];
  bool long_press_sent_[kNumSwitches];
  bool used_as_modifier_[kNumSwitches];
  uint32_t page_timeout_;
  uint32_t option_flash_until_;
  uint32_t panic_until_;

  uint8_t menu_mode_;
  uint8_t menu_bank_;
  bool menu_follow_;     // POSITION has moved since the menu opened
  float menu_pos0_;
  uint8_t menu_option_;
  uint8_t slot_;
  int8_t morph_slot_;
  uint8_t env_cursor_;

  volatile bool save_requested_;
  volatile bool settings_dirty_;

  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace monsoon

#endif  // MONSOON_UI_H_
