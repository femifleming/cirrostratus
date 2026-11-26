// Buttons and LEDs.
//
//   FREEZE            tap: freeze on/off
//   DISP              tap: LEDs show input level <-> WET/BAL/FBK/RVB
//   DISP (hold)       mode menu: POSITION picks the mode (shown in binary,
//                     green = bank 1, modes 1-15; red = bank 2, 16-30),
//                     WRITE tap switches bank, FREEZE cycles the mode's
//                     option, release DISP to apply
//   DISP hold + WRITE hold 1 s   V/OCT calibration (as on stock firmware)
//   WRITE             tap: load page   hold: save page
//                     (DISP picks the slot, WRITE confirms; in the load page
//                      FREEZE arms/clears the slot as the morph source)
//   FREEZE hold + DISP   envelope page: DISP moves the cursor over
//                     WET/BAL/FBK/RVB, FREEZE cycles off/+½/+1/−½/−1,
//                     WRITE saves and leaves
//   FREEZE hold + WRITE  mono bass on/off (main out, below 60 Hz); the LEDs
//                     show it for 1.5 s: middle two green = on, outer two
//                     amber = off. Stored in the settings.
//   WRITE hold + DISP    buffer page (tap DISP within a second of pressing
//                     WRITE): the master buffer quality for every mode
//                     that records (STRETCH, CAPTURE, TAPE,
//                     BARBERPOLE, BLOOM). FREEZE steps through
//                     48k/24k/16k 16-bit, then 48k/24k/16k 8-bit. LEDs:
//                     1-3 lit = 48/24/16 kHz, green = 16-bit, red = 8-bit;
//                     LED 4 blinks amber while the choice differs from
//                     the current one. WRITE applies (the running mode's
//                     buffer restarts empty), DISP or 6 s cancels.

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
  UI_PAGE_MONO_BASS,
  UI_PAGE_BUFFER,
  UI_PAGE_FFT,
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
  void PaintSplash(uint32_t now);
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
  uint32_t mono_bass_until_;
  uint32_t page_opened_;          // when the buffer / FFT page opened
  uint32_t applied_until_;        // > 0: showing the applied setting
  bool save_by_hold_;             // the save page came from holding WRITE

  uint8_t menu_mode_;
  uint8_t menu_bank_;
  bool menu_follow_;     // POSITION has moved since the menu opened
  float menu_pos0_;
  // The POSITION range the menu has seen (see ModeFromPosition).
  float menu_lo_;
  float menu_hi_;
  uint8_t menu_option_;
  uint8_t slot_;
  int8_t morph_slot_;
  uint8_t env_cursor_;
  uint8_t buffer_cursor_;
  uint8_t fft_cursor_;

  void OpenBufferPage(uint32_t now);
  void OpenFftPage(uint32_t now);
  bool SettingPage() const {
    return page_ == UI_PAGE_BUFFER || page_ == UI_PAGE_FFT;
  }

  volatile bool save_requested_;
  volatile bool settings_dirty_;

  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace monsoon

#endif  // MONSOON_UI_H_
