#include "monsoon/ui.h"

#include <math.h>

#include "stmlib/system/system_clock.h"

#include "monsoon/cv_scaler.h"
#include "monsoon/meter.h"
#include "monsoon/settings.h"

namespace monsoon {

using namespace stmlib;

const uint32_t kLongPress = 1000;
const uint32_t kPageTimeout = 6000;
const uint32_t kSplashDuration = 1200;
const int kModesPerBank = 15;

void Ui::Init(Settings* settings, CvScaler* cv_scaler, Engine* engine,
              Meter* meter) {
  settings_ = settings;
  cv_scaler_ = cv_scaler;
  engine_ = engine;
  meter_ = meter;
  leds_.Init();
  switches_.Init();
  page_ = UI_PAGE_SPLASH;
  previous_page_ = UI_PAGE_VU_METER;
  for (int i = 0; i < kNumSwitches; ++i) {
    press_time_[i] = 0;
    long_press_sent_[i] = false;
    used_as_modifier_[i] = false;
  }
  page_timeout_ = 0;
  option_flash_until_ = 0;
  slot_ = 0;
  morph_slot_ = -1;
  env_cursor_ = 0;
  save_requested_ = false;
  settings_dirty_ = false;
  panic_until_ = 0;

  const SettingsData& d = settings_->data();
  menu_mode_ = d.mode;
  menu_bank_ = d.mode / kModesPerBank;
  menu_option_ = d.option[d.mode];
  if (d.display == 1) previous_page_ = UI_PAGE_KNOBS;
  engine_->RequestMode(static_cast<ModeId>(d.mode), d.option[d.mode]);
  engine_->set_env_routing(settings_->env_routing(d.mode));
}

void Ui::Poll() {
  system_clock.Tick();
  switches_.Debounce();
  uint32_t now = system_clock.milliseconds();
  for (uint8_t i = 0; i < kNumSwitches; ++i) {
    if (switches_.just_pressed(i)) {
      press_time_[i] = now;
      long_press_sent_[i] = false;
      used_as_modifier_[i] = false;
      OnPress(i);
    } else if (switches_.pressed(i) && press_time_[i] && !long_press_sent_[i] &&
               now - press_time_[i] >= kLongPress) {
      long_press_sent_[i] = true;
      OnLongPress(i);
    } else if (switches_.released(i) && press_time_[i]) {
      uint32_t duration = now - press_time_[i];
      press_time_[i] = 0;
      OnRelease(i, duration);
    }
  }
  if (page_ == UI_PAGE_SPLASH && now > kSplashDuration) {
    page_ = previous_page_;
  }
  if ((page_ == UI_PAGE_LOAD || page_ == UI_PAGE_SAVE ||
       page_ == UI_PAGE_ENV) && now > page_timeout_) {
    if (page_ == UI_PAGE_ENV) settings_dirty_ = true;
    page_ = previous_page_;
  }
  PaintLeds();
}

void Ui::OnPress(uint8_t id) {
  page_timeout_ = system_clock.milliseconds() + kPageTimeout;
  switch (id) {
    case SWITCH_FREEZE:
      if (page_ == UI_PAGE_MODE_MENU) {
        uint8_t n = engine_->num_options(static_cast<ModeId>(menu_mode_));
        menu_option_ = (menu_option_ + 1) % n;
        option_flash_until_ = system_clock.milliseconds() + 1500;
        used_as_modifier_[SWITCH_FREEZE] = true;
      } else if (page_ == UI_PAGE_LOAD) {
        // Arm (or clear) this slot as the morph source.
        if (morph_slot_ == slot_) {
          morph_slot_ = -1;
          engine_->SetMorphSource(NULL, 0);
        } else if (engine_->supports_morph()) {
          morph_slot_ = slot_;
          engine_->SetMorphSource(settings_->slot_data(slot_), kSlotBytes);
        }
        used_as_modifier_[SWITCH_FREEZE] = true;
      } else if (page_ == UI_PAGE_ENV) {
        int8_t* e = &settings_->mutable_data()->env[menu_mode_][env_cursor_];
        // off -> +1/2 -> +1 -> -1/2 -> -1 -> off
        static const int8_t next[5] = { 1, 2, -1, -2, 0 };
        int index = *e == 0 ? 0 : *e == 1 ? 1 : *e == 2 ? 2 : *e == -1 ? 3 : 4;
        *e = next[index];
        engine_->set_env_routing(settings_->env_routing(menu_mode_));
        used_as_modifier_[SWITCH_FREEZE] = true;
      }
      break;

    case SWITCH_DISP:
      if (switches_.pressed(SWITCH_FREEZE) &&
          (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS)) {
        used_as_modifier_[SWITCH_FREEZE] = true;
        used_as_modifier_[SWITCH_DISP] = true;
        menu_mode_ = engine_->mode();
        env_cursor_ = 0;
        previous_page_ = page_;
        page_ = UI_PAGE_ENV;
      } else if (page_ == UI_PAGE_LOAD || page_ == UI_PAGE_SAVE) {
        slot_ = (slot_ + 1) % kNumSlots;
        used_as_modifier_[SWITCH_DISP] = true;
      } else if (page_ == UI_PAGE_ENV) {
        env_cursor_ = (env_cursor_ + 1) % ENV_TARGET_LAST;
        used_as_modifier_[SWITCH_DISP] = true;
      }
      break;

    default:
      break;
  }
}

void Ui::OnLongPress(uint8_t id) {
  if (id == SWITCH_DISP && !used_as_modifier_[SWITCH_DISP] &&
      (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS)) {
    previous_page_ = page_;
    menu_mode_ = engine_->mode();
    menu_bank_ = menu_mode_ / kModesPerBank;
    menu_option_ = engine_->option();
    cv_scaler_->set_hold_position(true);
    page_ = UI_PAGE_MODE_MENU;
  } else if (id == SWITCH_WRITE) {
    if (page_ == UI_PAGE_MODE_MENU) {
      // DISP held + WRITE held: calibration, leave the menu unchanged.
      cv_scaler_->set_hold_position(false);
      used_as_modifier_[SWITCH_DISP] = true;
      used_as_modifier_[SWITCH_WRITE] = true;
      page_ = UI_PAGE_CALIBRATION_1;
    } else if (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS) {
      previous_page_ = page_;
      used_as_modifier_[SWITCH_WRITE] = true;
      page_ = UI_PAGE_SAVE;
    }
  }
}

void Ui::OnRelease(uint8_t id, uint32_t duration) {
  (void)duration;
  page_timeout_ = system_clock.milliseconds() + kPageTimeout;
  bool modifier = used_as_modifier_[id];
  switch (id) {
    case SWITCH_FREEZE:
      if (!modifier && page_ != UI_PAGE_MODE_MENU && page_ != UI_PAGE_LOAD &&
          page_ != UI_PAGE_ENV) {
        cv_scaler_->ToggleFreezeButton();
      }
      break;

    case SWITCH_DISP:
      if (page_ == UI_PAGE_MODE_MENU) {
        ApplyModeMenu();
        cv_scaler_->set_hold_position(false);
        page_ = previous_page_;
      } else if (!modifier) {
        if (page_ == UI_PAGE_VU_METER) {
          page_ = UI_PAGE_KNOBS;
          settings_->mutable_data()->display = 1;
        } else if (page_ == UI_PAGE_KNOBS) {
          page_ = UI_PAGE_VU_METER;
          settings_->mutable_data()->display = 0;
        }
      }
      break;

    case SWITCH_WRITE:
      if (modifier) break;
      if (page_ == UI_PAGE_MODE_MENU) {
        // Tap WRITE in the menu: other bank. DoEvents re-reads POSITION.
        menu_bank_ = menu_bank_ ? 0 : 1;
        if (menu_bank_ * kModesPerBank >= MODE_LAST) menu_bank_ = 0;
        menu_mode_ = 0xff;
      } else if (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS) {
        previous_page_ = page_;
        page_ = UI_PAGE_LOAD;
      } else if (page_ == UI_PAGE_LOAD) {
        engine_->RequestLoad(settings_->slot_data(slot_), kSlotBytes);
        page_ = previous_page_;
      } else if (page_ == UI_PAGE_SAVE) {
        page_ = UI_PAGE_SAVING;
        save_requested_ = true;
      } else if (page_ == UI_PAGE_ENV) {
        settings_dirty_ = true;
        page_ = previous_page_;
      } else if (page_ == UI_PAGE_CALIBRATION_1) {
        cv_scaler_->CalibrateC1();
        cv_scaler_->CalibrateOffsets();
        page_ = UI_PAGE_CALIBRATION_2;
      } else if (page_ == UI_PAGE_CALIBRATION_2) {
        if (cv_scaler_->CalibrateC3()) {
          settings_dirty_ = true;
          page_ = previous_page_;
        } else {
          page_ = UI_PAGE_PANIC;
        }
      }
      break;
  }
}

uint8_t Ui::ModeFromPosition() const {
  // POSITION spans the 15 modes of the current bank.
  float p = cv_scaler_->raw_position();
  int zone = static_cast<int>(p * kModesPerBank);
  if (zone < 0) zone = 0;
  if (zone >= kModesPerBank) zone = kModesPerBank - 1;
  int m = menu_bank_ * kModesPerBank + zone;
  if (m >= MODE_LAST) m = MODE_LAST - 1;
  return static_cast<uint8_t>(m);
}

void Ui::ApplyModeMenu() {
  if (menu_mode_ >= MODE_LAST) menu_mode_ = ModeFromPosition();
  SettingsData* d = settings_->mutable_data();
  bool mode_changed = menu_mode_ != engine_->mode();
  if (mode_changed) {
    // Coming into a mode: use the option it was left with last time,
    // unless FREEZE was used in the menu to pick one.
    if (option_flash_until_ == 0) menu_option_ = d->option[menu_mode_];
  }
  d->mode = menu_mode_;
  d->option[menu_mode_] = menu_option_;
  engine_->RequestMode(static_cast<ModeId>(menu_mode_), menu_option_);
  engine_->set_env_routing(settings_->env_routing(menu_mode_));
  if (mode_changed) {
    morph_slot_ = -1;
  }
  option_flash_until_ = 0;
  settings_dirty_ = true;
}

void Ui::SaveSlot() {
  PersistentChunk chunks[kMaxPersistentChunks + 1];
  engine_->set_silence(true);
  system_clock.Delay(20);
  size_t n = engine_->GetPersistentData(chunks);
  if (n) {
    settings_->WriteSlot(slot_, chunks, n);
  }
  engine_->set_silence(false);
  slot_ = (slot_ + 1) % kNumSlots;
}

void Ui::DoEvents() {
  if (page_ == UI_PAGE_MODE_MENU) {
    uint8_t m = ModeFromPosition();
    if (m != menu_mode_) {
      menu_mode_ = m;
      menu_option_ = settings_->data().option[m];
      option_flash_until_ = 0;
    }
  }
  if (save_requested_) {
    save_requested_ = false;
    SaveSlot();
    page_ = previous_page_;
  }
  if (settings_dirty_) {
    settings_dirty_ = false;
    engine_->set_silence(true);
    system_clock.Delay(5);
    settings_->Save();
    engine_->set_silence(false);
  }
  if (engine_->init_failed() && page_ != UI_PAGE_PANIC) {
    page_ = UI_PAGE_PANIC;
  }
  // A failed calibration shows red for a moment, then returns.
  if (page_ == UI_PAGE_PANIC && !engine_->init_failed()) {
    if (!panic_until_) panic_until_ = system_clock.milliseconds() + 1500;
    if (system_clock.milliseconds() > panic_until_) {
      panic_until_ = 0;
      page_ = UI_PAGE_VU_METER;
    }
  }
}

void Ui::ShowBinary(uint8_t value, bool green, uint8_t brightness) {
  for (int i = 0; i < 4; ++i) {
    bool on = value & (8 >> i);
    leds_.set_status(i, on && !green ? brightness : 0,
                     on && green ? brightness : 0);
  }
}

void Ui::PaintLeds() {
  leds_.Clear();
  uint32_t now = system_clock.milliseconds();
  bool blink = (now & 255) > 127;
  bool fast_blink = (now & 127) > 63;
  switch (page_) {
    case UI_PAGE_SPLASH:
      {
        // Rain: a drop falls across the four LEDs.
        uint8_t index = (now >> 7) & 3;
        leds_.set_status(index, 0, 255);
        leds_.set_status((index + 3) & 3, 0, 60);
      }
      break;

    case UI_PAGE_VU_METER:
      {
        int32_t peak = meter_->peak();
        // 0 dB at full scale, ~-48 dB at the bottom.
        float db = 20.0f * log10f(static_cast<float>(peak) / 32767.0f + 1e-6f);
        int32_t bar = static_cast<int32_t>((db + 48.0f) / 48.0f * 32767.0f);
        if (bar > 0) leds_.PaintBar(bar);
      }
      break;

    case UI_PAGE_KNOBS:
      leds_.set_intensity(0, static_cast<uint8_t>(cv_scaler_->wet() * 255.0f));
      leds_.set_intensity(1, static_cast<uint8_t>(cv_scaler_->stereo() * 255.0f));
      leds_.set_intensity(2, static_cast<uint8_t>(cv_scaler_->feedback() * 255.0f));
      leds_.set_intensity(3, static_cast<uint8_t>(cv_scaler_->reverb() * 255.0f));
      break;

    case UI_PAGE_MODE_MENU:
      if (option_flash_until_ && now < option_flash_until_) {
        uint8_t o = menu_option_;
        if (blink) {
          leds_.set_status(o & 3, o >= 4 ? 255 : 0, o >= 4 ? 0 : 255);
        }
      } else {
        // Bank 1 green 1-15, bank 2 red 1-15 (= modes 16-30).
        ShowBinary(menu_mode_ % kModesPerBank + 1,
                   menu_mode_ < kModesPerBank, 255);
      }
      break;

    case UI_PAGE_LOAD:
      if (morph_slot_ >= 0) leds_.set_status(morph_slot_, 255, 0);
      leds_.set_status(slot_, morph_slot_ == slot_ ? 255 : 0, blink ? 255 : 0);
      break;

    case UI_PAGE_SAVE:
      leds_.set_status(slot_, blink ? 255 : 0, 0);
      break;

    case UI_PAGE_SAVING:
      leds_.set_status(slot_, 255, 0);
      break;

    case UI_PAGE_ENV:
      for (int i = 0; i < ENV_TARGET_LAST; ++i) {
        int8_t e = settings_->data().env[menu_mode_][i];
        uint8_t level = (e == 2 || e == -2) ? 255 : 70;
        uint8_t red = e < 0 ? level : 0;
        uint8_t green = e > 0 ? level : 0;
        if (i == env_cursor_ && fast_blink) {
          // Cursor: blink, show a faint amber when the target is off.
          if (e == 0) { red = 40; green = 40; } else { red = 0; green = 0; }
        }
        leds_.set_status(i, red, green);
      }
      break;

    case UI_PAGE_CALIBRATION_1:
      leds_.set_status(0, blink ? 255 : 0, blink ? 255 : 0);
      leds_.set_status(1, blink ? 255 : 0, blink ? 255 : 0);
      break;

    case UI_PAGE_CALIBRATION_2:
      for (int i = 0; i < 4; ++i) {
        leds_.set_status(i, blink ? 255 : 0, blink ? 255 : 0);
      }
      break;

    case UI_PAGE_PANIC:
      for (int i = 0; i < 4; ++i) leds_.set_status(i, 255, 0);
      break;
  }
  leds_.set_freeze(cv_scaler_->freeze());
  leds_.Write();
}

}  // namespace monsoon
