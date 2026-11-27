// Buttons and LEDs, Typhoon build. The Monsoon UI (monsoon/ui.h) with the
// Typhoon's buttons and LEDs (docs/TYPHOON.md has the full map):
//
//   TIME   = the Monsoon's DISP button, every gesture
//   BANK   = the Monsoon's WRITE button, every gesture
//   FREEZE = FREEZE (HOLD jack = FREEZE gate)
//   TRIG button = a TRIG pulse (like the jack); held at power-up: V/OCT
//            calibration (as on the stock firmware)
//   IN MUTE / OUT MUTE tap: click-free mute of the input / the output
//   IN MUTE + OUT MUTE held 1 s: AUX random voltage on/off
//
//   TIME LEDs 1 2 4 8: the mode number in binary (and the bars of the
//                      buffer / FFT pages, the option number in the menu)
//   BANK LEDs A B C D: bank (A = bank 1, B = bank 2), save slots 1-4,
//                      WET/BAL/FBK/RVB on the knobs page
//   IN / OUT rows:     input and output level meters; the row's top LED
//                      blinks while that side is muted

#ifndef TYPHOON_UI_H_
#define TYPHOON_UI_H_

#include "stmlib/stmlib.h"

#include "typhoon/drivers/dac.h"
#include "typhoon/drivers/leds.h"
#include "typhoon/drivers/switches.h"
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
  UI_PAGE_AUX,
  UI_PAGE_CALIBRATION_1,
  UI_PAGE_CALIBRATION_2,
  UI_PAGE_PANIC,
};

class CvScaler;
class Meter;
class Mute;
class Settings;

class Ui {
 public:
  Ui() { }
  ~Ui() { }

  void Init(Settings* settings, CvScaler* cv_scaler, Engine* engine,
            Meter* in_meter, Meter* out_meter, Mute* mute_in,
            Mute* mute_out, Dac* dac);
  void Poll();      // 1 kHz, from SysTick
  void DoEvents();  // main loop
  void Panic() { page_ = UI_PAGE_PANIC; }

 private:
  void OnPress(uint8_t id);
  void OnLongPress(uint8_t id);
  void OnRelease(uint8_t id, uint32_t duration);
  void CalibrationStep();
  void PaintLeds();
  void PaintSplash(uint32_t now);
  void PaintMeters(bool blink);
  void ShowBinary(uint8_t value, uint8_t brightness);
  void ShowMode(uint8_t mode, uint8_t brightness);
  void UpdateAux(uint32_t now);
  void ApplyModeMenu();
  void SaveSlot();
  uint8_t ModeFromPosition() const;

  Settings* settings_;
  CvScaler* cv_scaler_;
  Engine* engine_;
  Meter* in_meter_;
  Meter* out_meter_;
  Mute* mute_in_;
  Mute* mute_out_;
  Leds leds_;
  Switches switches_;
  Dac* dac_;

  volatile UiPage page_;
  UiPage previous_page_;
  uint32_t press_time_[kNumSwitches];
  bool long_press_sent_[kNumSwitches];
  bool used_as_modifier_[kNumSwitches];
  uint32_t page_timeout_;
  uint32_t option_flash_until_;
  uint32_t panic_until_;
  uint32_t mono_bass_until_;
  uint32_t aux_until_;
  uint32_t page_opened_;          // when the buffer / FFT page opened
  uint32_t applied_until_;        // > 0: showing the applied setting
  bool save_by_hold_;             // the save page came from holding WRITE
  bool boot_calibration_;         // TRIG was held at power-up

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

  // AUX random voltage (DAC on PA4): a slow smoothed random walk.
  uint32_t aux_rng_;
  float aux_value_;
  float aux_target_;

  void OpenBufferPage(uint32_t now);
  void OpenFftPage(uint32_t now);
  bool SettingPage() const {
    return page_ == UI_PAGE_BUFFER || page_ == UI_PAGE_FFT;
  }
  bool CalibrationPage() const {
    return page_ == UI_PAGE_CALIBRATION_1 || page_ == UI_PAGE_CALIBRATION_2;
  }

  volatile bool save_requested_;
  volatile bool settings_dirty_;

  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace monsoon

#endif  // TYPHOON_UI_H_
