#include "typhoon/ui.h"

#include <math.h>

#include "stmlib/system/system_clock.h"

#include "typhoon/cv_scaler.h"
#include "typhoon/meter.h"
#include "typhoon/mute.h"
#include "typhoon/settings.h"

namespace monsoon {

using namespace stmlib;

const uint32_t kLongPress = 1000;
const uint32_t kPageTimeout = 6000;
const uint32_t kSplashDuration = 3300;
const int kModesPerBank = 15;
const float kMenuLo = 0.12f;
const float kMenuHi = 0.88f;

// The current mode, shown dimly on the TIME / BANK columns on the meter and
// knobs pages (the Monsoon has no LEDs to spare for this).
const uint8_t kIdleModeLevel = 40;
// AUX random voltage: a new target every 500 ms, ~200 ms glide, at most
// 1000/4095 of the DAC (~0.8 V, ~1.2 V after the x1.5 AUX amplifier).
const uint32_t kAuxStepMs = 500;
const float kAuxDepth = 1000.0f;

void Ui::Init(Settings* settings, CvScaler* cv_scaler, Engine* engine,
              Meter* in_meter, Meter* out_meter, Mute* mute_in,
              Mute* mute_out, Dac* dac) {
  settings_ = settings;
  cv_scaler_ = cv_scaler;
  engine_ = engine;
  in_meter_ = in_meter;
  out_meter_ = out_meter;
  mute_in_ = mute_in;
  mute_out_ = mute_out;
  dac_ = dac;  // initialised (0 V) first thing in Init(), typhoon.cc
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
  buffer_cursor_ = 0;
  fft_cursor_ = 0;
  save_requested_ = false;
  settings_dirty_ = false;
  panic_until_ = 0;
  mono_bass_until_ = 0;
  aux_until_ = 0;
  page_opened_ = 0;
  applied_until_ = 0;
  save_by_hold_ = false;
  menu_lo_ = kMenuLo;
  menu_hi_ = kMenuHi;
  aux_rng_ = 0x2545f491u;
  aux_value_ = 0.0f;
  aux_target_ = 0.0f;

  const SettingsData& d = settings_->data();
  menu_mode_ = d.mode;
  menu_bank_ = d.mode / kModesPerBank;
  menu_option_ = d.option[d.mode];
  if (d.display == 1) previous_page_ = UI_PAGE_KNOBS;
  engine_->RequestMode(static_cast<ModeId>(d.mode), d.option[d.mode]);
  engine_->set_env_routing(settings_->env_routing(d.mode));
  engine_->set_mono_bass(settings_->mono_bass(), true);
  engine_->set_buffer_quality(d.buffer_quality);
  engine_->set_fft_setting(d.fft_setting);

  // TRIG held at power-up: V/OCT calibration, as on the stock firmware.
  // (Give the pull-down a moment after the pin was configured.)
  for (volatile int i = 0; i < 2000; ++i) { }
  boot_calibration_ = switches_.pressed_immediate(SWITCH_TRIG);
  if (boot_calibration_) {
    page_ = UI_PAGE_CALIBRATION_1;
  }
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
  cv_scaler_->set_trig_button(switches_.pressed(SWITCH_TRIG));
  if (page_ == UI_PAGE_SPLASH && now > kSplashDuration) {
    page_ = previous_page_;
  }
  if ((page_ == UI_PAGE_LOAD || page_ == UI_PAGE_SAVE ||
       page_ == UI_PAGE_ENV || SettingPage()) &&
      now > page_timeout_) {
    if (page_ == UI_PAGE_ENV) settings_dirty_ = true;
    page_ = previous_page_;
  }
  if (SettingPage() && applied_until_ && now > applied_until_) {
    applied_until_ = 0;
    page_ = previous_page_;
  }
  if (page_ == UI_PAGE_MONO_BASS && now > mono_bass_until_) {
    page_ = previous_page_;
  }
  if (page_ == UI_PAGE_AUX && now > aux_until_) {
    page_ = previous_page_;
  }
  UpdateAux(now);
  PaintLeds();
}

void Ui::UpdateAux(uint32_t now) {
  // Off while calibrating: the 0 V offsets are read from the unpatched
  // jacks, which see the AUX normal.
  bool active = settings_->data().aux_random && !CalibrationPage();
  dac_->set_enabled(active);
  if (!active) {
    aux_value_ = 0.0f;
    return;
  }
  if (now % kAuxStepMs == 0) {
    aux_rng_ = aux_rng_ * 1664525u + 1013904223u;
    aux_target_ = static_cast<float>(aux_rng_ >> 8) / 16777216.0f;
  }
  aux_value_ += 0.005f * (aux_target_ - aux_value_);
  dac_->Write(static_cast<uint16_t>(aux_value_ * kAuxDepth));
}

void Ui::OpenBufferPage(uint32_t now) {
  used_as_modifier_[SWITCH_WRITE] = true;
  used_as_modifier_[SWITCH_DISP] = true;
  buffer_cursor_ = settings_->data().buffer_quality;
  page_opened_ = now;
  applied_until_ = 0;
  save_by_hold_ = false;
  page_ = UI_PAGE_BUFFER;
}

void Ui::OpenFftPage(uint32_t now) {
  used_as_modifier_[SWITCH_DISP] = true;
  used_as_modifier_[SWITCH_FREEZE] = true;
  fft_cursor_ = settings_->data().fft_setting;
  page_opened_ = now;
  applied_until_ = 0;
  page_ = UI_PAGE_FFT;
}

void Ui::OnPress(uint8_t id) {
  page_timeout_ = system_clock.milliseconds() + kPageTimeout;
  if (applied_until_) {
    // Any press ends the "applied" confirmation and acts as usual.
    applied_until_ = 0;
    if (SettingPage()) page_ = previous_page_;
  }
  if (page_ == UI_PAGE_SPLASH) {
    page_ = previous_page_;
    used_as_modifier_[id] = true;  // the skipping press does nothing else
    return;
  }
  switch (id) {
    case SWITCH_FREEZE:
      if (page_ == UI_PAGE_MODE_MENU) {
        if (!ModeExists(menu_mode_)) menu_mode_ = ModeFromPosition();
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
      } else if (page_ == UI_PAGE_BUFFER) {
        buffer_cursor_ = (buffer_cursor_ + 1) % kNumBufferQualities;
        used_as_modifier_[SWITCH_FREEZE] = true;
      } else if (page_ == UI_PAGE_FFT) {
        fft_cursor_ = (fft_cursor_ + 1) % kNumFftSettings;
        used_as_modifier_[SWITCH_FREEZE] = true;
      } else if (switches_.pressed(SWITCH_DISP) &&
                 (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS)) {
        // DISP held + FREEZE: FFT page.
        previous_page_ = page_;
        OpenFftPage(system_clock.milliseconds());
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
      } else if (switches_.pressed(SWITCH_WRITE) &&
                 (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS)) {
        // WRITE held + DISP: buffer page.
        previous_page_ = page_;
        OpenBufferPage(system_clock.milliseconds());
      } else if (switches_.pressed(SWITCH_WRITE) && page_ == UI_PAGE_SAVE &&
                 save_by_hold_) {
        // WRITE held past a second opened the save page first: DISP while
        // WRITE is still down still means the buffer page (previous_page_
        // is already the page the save page came from).
        OpenBufferPage(system_clock.milliseconds());
      } else if (page_ == UI_PAGE_LOAD || page_ == UI_PAGE_SAVE) {
        slot_ = (slot_ + 1) % kNumSlots;
        used_as_modifier_[SWITCH_DISP] = true;
      } else if (page_ == UI_PAGE_ENV) {
        env_cursor_ = (env_cursor_ + 1) % ENV_TARGET_LAST;
        used_as_modifier_[SWITCH_DISP] = true;
      }
      break;

    case SWITCH_WRITE:
      if (switches_.pressed(SWITCH_FREEZE) &&
          (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS)) {
        // FREEZE held + WRITE: mono bass on/off.
        used_as_modifier_[SWITCH_FREEZE] = true;
        used_as_modifier_[SWITCH_WRITE] = true;
        SettingsData* d = settings_->mutable_data();
        d->mono_bass_off = d->mono_bass_off ? 0 : 1;
        engine_->set_mono_bass(settings_->mono_bass());
        settings_dirty_ = true;
        previous_page_ = page_;
        page_ = UI_PAGE_MONO_BASS;
        mono_bass_until_ = system_clock.milliseconds() + 1500;
      }
      break;

    case SWITCH_TRIG:
      // The press itself reaches the engine through set_trig_button().
      if (boot_calibration_) {
        // Still the press held through power-up: its release must not
        // count as the 1 V step.
        boot_calibration_ = false;
        used_as_modifier_[SWITCH_TRIG] = true;
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
    menu_follow_ = false;
    menu_pos0_ = cv_scaler_->raw_position();
    cv_scaler_->set_hold_position(true);
    page_ = UI_PAGE_MODE_MENU;
  } else if (id == SWITCH_WRITE) {
    if (page_ == UI_PAGE_MODE_MENU) {
      // DISP held + WRITE held: calibration, leave the menu unchanged.
      cv_scaler_->set_hold_position(false);
      used_as_modifier_[SWITCH_DISP] = true;
      used_as_modifier_[SWITCH_WRITE] = true;
      page_ = UI_PAGE_CALIBRATION_1;
    } else if (!used_as_modifier_[SWITCH_WRITE] &&
               (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS)) {
      previous_page_ = page_;
      used_as_modifier_[SWITCH_WRITE] = true;
      save_by_hold_ = true;
      page_ = UI_PAGE_SAVE;
    }
  } else if ((id == SWITCH_MUTE_IN || id == SWITCH_MUTE_OUT) &&
             switches_.pressed(SWITCH_MUTE_IN) &&
             switches_.pressed(SWITCH_MUTE_OUT) &&
             !used_as_modifier_[SWITCH_MUTE_IN] &&
             !used_as_modifier_[SWITCH_MUTE_OUT] &&
             (page_ == UI_PAGE_VU_METER || page_ == UI_PAGE_KNOBS)) {
    // Both mute buttons held for a second: AUX random voltage on/off.
    used_as_modifier_[SWITCH_MUTE_IN] = true;
    used_as_modifier_[SWITCH_MUTE_OUT] = true;
    SettingsData* d = settings_->mutable_data();
    d->aux_random = d->aux_random ? 0 : 1;
    settings_dirty_ = true;
    previous_page_ = page_;
    page_ = UI_PAGE_AUX;
    aux_until_ = system_clock.milliseconds() + 1500;
  }
}

void Ui::CalibrationStep() {
  if (page_ == UI_PAGE_CALIBRATION_1) {
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
}

void Ui::OnRelease(uint8_t id, uint32_t duration) {
  page_timeout_ = system_clock.milliseconds() + kPageTimeout;
  bool modifier = used_as_modifier_[id];
  switch (id) {
    case SWITCH_FREEZE:
      if (!modifier && page_ != UI_PAGE_MODE_MENU && page_ != UI_PAGE_LOAD &&
          page_ != UI_PAGE_ENV && !SettingPage()) {
        cv_scaler_->ToggleFreezeButton();
      }
      break;

    case SWITCH_DISP:
      if (page_ == UI_PAGE_MODE_MENU) {
        ApplyModeMenu();
        cv_scaler_->set_hold_position(false);
        page_ = previous_page_;
      } else if (SettingPage()) {
        if (!modifier) page_ = previous_page_;  // cancel
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
      save_by_hold_ = false;
      if (modifier) break;
      if (page_ == UI_PAGE_MODE_MENU) {
        // Tap WRITE in the menu: other bank. DoEvents re-reads POSITION.
        menu_bank_ = menu_bank_ ? 0 : 1;
        if (menu_bank_ * kModesPerBank >= MODE_LAST) menu_bank_ = 0;
        menu_follow_ = true;
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
      } else if (page_ == UI_PAGE_BUFFER) {
        SettingsData* d = settings_->mutable_data();
        if (d->buffer_quality != buffer_cursor_) {
          d->buffer_quality = buffer_cursor_;
          settings_dirty_ = true;
        }
        engine_->set_buffer_quality(buffer_cursor_);
        // Stay a moment to show it took, then back.
        applied_until_ = system_clock.milliseconds() + 800;
      } else if (page_ == UI_PAGE_FFT) {
        SettingsData* d = settings_->mutable_data();
        if (d->fft_setting != fft_cursor_) {
          d->fft_setting = fft_cursor_;
          settings_dirty_ = true;
        }
        engine_->set_fft_setting(fft_cursor_);
        applied_until_ = system_clock.milliseconds() + 800;
      } else if (CalibrationPage()) {
        CalibrationStep();
      }
      break;

    case SWITCH_TRIG:
      // On the calibration pages TRIG confirms each step, like WRITE (the
      // stock firmware's way).
      if (!modifier && CalibrationPage()) {
        CalibrationStep();
      }
      break;

    case SWITCH_MUTE_IN:
      if (!modifier && duration < kLongPress) mute_in_->toggle();
      break;

    case SWITCH_MUTE_OUT:
      if (!modifier && duration < kLongPress) mute_out_->toggle();
      break;

    default:
      break;
  }
}

uint8_t Ui::ModeFromPosition() const {
  // POSITION spans the 15 modes of the current bank. The zones span the
  // range the menu has actually seen: at least kMenuLo..kMenuHi, widened
  // whenever the slider goes further. (On the Typhoon this is the slider
  // alone; its CV jack is read separately and plays no part here.)
  float p = cv_scaler_->raw_position();
  float q = (p - menu_lo_) / (menu_hi_ - menu_lo_);
  int first = menu_bank_ * kModesPerBank;
  int last = first + kModesPerBank;
  if (first < MODE_FIRST) first = MODE_FIRST;
  if (last > MODE_LAST) last = MODE_LAST;
  const int count = last - first;
  int zone = static_cast<int>(q * count);
  if (zone < 0) zone = 0;
  if (zone >= count) zone = count - 1;
  return static_cast<uint8_t>(first + zone);
}

void Ui::ApplyModeMenu() {
  if (!ModeExists(menu_mode_)) menu_mode_ = ModeFromPosition();
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
    // Only follow POSITION once it has really moved (or the bank changed),
    // so opening the menu just to look never changes the mode.
    if (!menu_follow_ && fabsf(cv_scaler_->raw_position() - menu_pos0_) > 0.03f) {
      menu_follow_ = true;
    }
  }
  if (page_ == UI_PAGE_MODE_MENU) {
    // Learn the slider's real ends while the menu is open.
    float p = cv_scaler_->raw_position();
    p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    if (p < menu_lo_) menu_lo_ = p;
    if (p > menu_hi_) menu_hi_ = p;
  }
  if (page_ == UI_PAGE_MODE_MENU && menu_follow_) {
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
  // A failed calibration shows for a moment, then returns.
  if (page_ == UI_PAGE_PANIC && !engine_->init_failed()) {
    if (!panic_until_) panic_until_ = system_clock.milliseconds() + 1500;
    if (system_clock.milliseconds() > panic_until_) {
      panic_until_ = 0;
      page_ = UI_PAGE_VU_METER;
    }
  }
}

// A number on the TIME LEDs, labelled 1 2 4 8 (top to bottom).
void Ui::ShowBinary(uint8_t value, uint8_t brightness) {
  for (uint8_t i = 0; i < kLedsPerGroup; ++i) {
    leds_.set(LED_TIME, i, value & (1 << i) ? brightness : 0);
  }
}

// Mode number within its bank on TIME (1-15), the bank on BANK: A = bank 1
// (the Monsoon's green), B = bank 2 (its red).
void Ui::ShowMode(uint8_t mode, uint8_t brightness) {
  ShowBinary(mode % kModesPerBank + 1, brightness);
  leds_.set(LED_BANK, mode < kModesPerBank ? 0 : 1, brightness);
}

// 0 dB at full scale, ~-48 dB at the bottom (as the Monsoon's meter).
static int32_t PeakToBar(float peak) {
  float db = 20.0f * log10f(peak / 32767.0f + 1e-6f);
  return static_cast<int32_t>((db + 48.0f) / 48.0f * 32767.0f);
}

void Ui::PaintMeters(bool blink) {
  int32_t in_bar = PeakToBar(static_cast<float>(in_meter_->peak()));
  if (in_bar > 0) leds_.PaintBar(LED_IN, in_bar);
  float out = static_cast<float>(out_meter_->peak()) *
      cv_scaler_->out_meter_scale();
  int32_t out_bar = PeakToBar(out > 32767.0f ? 32767.0f : out);
  if (out_bar > 0) leds_.PaintBar(LED_OUT, out_bar);
  // Muted: the row's top (red) LED blinks.
  if (mute_in_->muted()) leds_.set(LED_IN, 3, blink ? 255 : 0);
  if (mute_out_->muted()) leds_.set(LED_OUT, 3, blink ? 255 : 0);
}

// Start-up sequence, "cirrostratus" (3.3 s, any button skips it). The
// Monsoon's, on all four groups of four LEDs at once:
//   0.0-0.8 s  ice crystals: dim sparkles twinkle at random
//   0.8-1.9 s  halo: two pulses from the middle LEDs of each group out to
//              the ends, the FREEZE LED glowing with each pulse
//   1.9-2.5 s  the sun breaks through: a sweep from half to full brightness
//   2.5-3.3 s  forecast: the current mode on TIME / BANK, fading
void Ui::PaintSplash(uint32_t now) {
  // Triangle 0..255..0 over `period` ms, offset by `phase` ms.
  struct Tri {
    static uint8_t at(int32_t t, int32_t period) {
      if (t < 0 || t >= period) return 0;
      int32_t h = period / 2;
      int32_t v = t < h ? t * 255 / h : (period - t) * 255 / h;
      return static_cast<uint8_t>(v);
    }
  };
  int32_t t = static_cast<int32_t>(now);
  if (t < 800) {
    // Each LED twinkles on its own pseudo-random schedule.
    for (uint8_t g = 0; g < kNumLedGroups; ++g) {
      for (uint8_t i = 0; i < kLedsPerGroup; ++i) {
        uint32_t k = g * kLedsPerGroup + i;
        uint32_t h = (static_cast<uint32_t>(t / 90) + k * 7919u) * 2654435761u;
        uint8_t level = (h >> 24) > 170 ? static_cast<uint8_t>((h >> 16) & 0x7f) : 0;
        // Fade the whole sparkle field in over the first 400 ms.
        level = static_cast<uint8_t>(level * (t < 400 ? t : 400) / 400);
        leds_.set(g, i, level);
      }
    }
    return;
  }
  if (t < 1900) {
    int32_t u = t - 800;
    uint8_t glow = 0;
    for (int pulse = 0; pulse < 2; ++pulse) {
      int32_t p = u - pulse * 500;
      uint8_t inner = Tri::at(p, 400);        // LEDs 2 and 3 (the middle)
      uint8_t outer = Tri::at(p - 150, 400);  // LEDs 1 and 4, a moment later
      if (inner || outer) {
        for (uint8_t g = 0; g < kNumLedGroups; ++g) {
          leds_.set(g, 1, inner);
          leds_.set(g, 2, inner);
          leds_.set(g, 0, outer);
          leds_.set(g, 3, outer);
        }
      }
      if (inner > glow) glow = inner;
    }
    leds_.set_freeze(glow > 128);
    return;
  }
  if (t < 2500) {
    // Half -> full brightness, sweeping along each group (the Monsoon's
    // amber -> green).
    int32_t u = t - 1900;
    for (uint8_t i = 0; i < kLedsPerGroup; ++i) {
      int32_t k = (u - i * 90) * 255 / 300;
      if (k < 0) k = 0;
      if (k > 255) k = 255;
      uint8_t level = static_cast<uint8_t>(96 + k * 159 / 255);
      for (uint8_t g = 0; g < kNumLedGroups; ++g) leds_.set(g, i, level);
    }
    return;
  }
  // The forecast: which mode is up, then fade out.
  int32_t u = t - 2500;
  uint8_t level = u < 500 ? 255 : static_cast<uint8_t>(255 - (u - 500) * 255 / 300);
  ShowMode(settings_->data().mode, level);
}

void Ui::PaintLeds() {
  leds_.Clear();
  uint32_t now = system_clock.milliseconds();
  bool blink = (now & 255) > 127;
  bool fast_blink = (now & 127) > 63;
  const UiPage page = page_;
  if (page != UI_PAGE_SPLASH && page != UI_PAGE_PANIC &&
      page != UI_PAGE_CALIBRATION_1 && page != UI_PAGE_CALIBRATION_2) {
    PaintMeters(blink);
  }
  switch (page) {
    case UI_PAGE_SPLASH:
      PaintSplash(now);
      break;

    case UI_PAGE_VU_METER:
      ShowMode(engine_->mode(), kIdleModeLevel);
      break;

    case UI_PAGE_KNOBS:
      // Monsoon: the four LEDs show WET / BAL / FBK / RVB as a colour
      // ramp. Here: brightness on BANK A / B / C / D.
      ShowBinary(engine_->mode() % kModesPerBank + 1, kIdleModeLevel);
      leds_.set(LED_BANK, 0, static_cast<uint8_t>(cv_scaler_->wet() * 255.0f));
      leds_.set(LED_BANK, 1, static_cast<uint8_t>(cv_scaler_->bal() * 255.0f));
      leds_.set(LED_BANK, 2, static_cast<uint8_t>(cv_scaler_->fbk() * 255.0f));
      leds_.set(LED_BANK, 3, static_cast<uint8_t>(cv_scaler_->rvb() * 255.0f));
      break;

    case UI_PAGE_MODE_MENU:
      if (option_flash_until_ && now < option_flash_until_) {
        // Monsoon: LED (o % 4) blinks, green for options 1-4, red for 5-8.
        // Here: the option number (1-8) blinks in binary on TIME.
        leds_.set(LED_BANK, menu_mode_ < kModesPerBank ? 0 : 1, 255);
        ShowBinary(menu_option_ + 1, blink ? 255 : 0);
      } else {
        // Bank 1: A + 1-15 in binary, bank 2: B + 1-15 (= modes 16-30).
        ShowMode(menu_mode_, 255);
      }
      break;

    case UI_PAGE_LOAD:
      // Monsoon: slot LED blinks green, the morph source stays red.
      // Here: the slot blinks on A-D, the morph source stays lit.
      if (morph_slot_ >= 0) leds_.set(LED_BANK, morph_slot_, 255);
      leds_.set(LED_BANK, slot_,
                blink ? 255 : (morph_slot_ == slot_ ? 40 : 0));
      break;

    case UI_PAGE_SAVE:
      // Monsoon: slot LED blinks red. Here: the slot blinks, the other
      // three glow dimly (so it can't be mistaken for the load page).
      leds_.set_group(LED_BANK, 24);
      leds_.set(LED_BANK, slot_, blink ? 255 : 0);
      break;

    case UI_PAGE_SAVING:
      leds_.set(LED_BANK, slot_, 255);
      break;

    case UI_PAGE_ENV:
      // Monsoon: LED i = WET / BAL / FBK / RVB, green = up, red = down,
      // dim = half. Here: row i (1/A = WET ... 8/D = RVB), the BANK LED
      // for up, the TIME LED for down, dim = half.
      for (int i = 0; i < ENV_TARGET_LAST; ++i) {
        int8_t e = settings_->data().env[menu_mode_][i];
        uint8_t level = (e == 2 || e == -2) ? 255 : 70;
        uint8_t down = e < 0 ? level : 0;
        uint8_t up = e > 0 ? level : 0;
        if (i == env_cursor_ && fast_blink) {
          // Cursor: blink, a faint glow on both sides when the target is off.
          if (e == 0) { down = 40; up = 40; } else { down = 0; up = 0; }
        }
        leds_.set(LED_TIME, i, down);
        leds_.set(LED_BANK, i, up);
      }
      break;

    case UI_PAGE_MONO_BASS:
      if (settings_->mono_bass()) {
        // Bass gathered in the middle (Monsoon: middle two green).
        leds_.set(LED_TIME, 1, 255);
        leds_.set(LED_TIME, 2, 255);
        leds_.set(LED_BANK, 1, 255);
        leds_.set(LED_BANK, 2, 255);
      } else {
        // Bass out at the sides (Monsoon: outer two amber).
        leds_.set(LED_TIME, 0, 255);
        leds_.set(LED_TIME, 3, 255);
        leds_.set(LED_BANK, 0, 255);
        leds_.set(LED_BANK, 3, 255);
      }
      break;

    case UI_PAGE_AUX:
      if (settings_->data().aux_random) {
        leds_.set_group(LED_TIME, 255);
        leds_.set_group(LED_BANK, 255);
      } else {
        leds_.set(LED_TIME, 3, 255);
        leds_.set(LED_BANK, 3, 255);
      }
      break;

    case UI_PAGE_BUFFER:
      {
        // Monsoon: an amber sweep, then LEDs 1-3 = 48 / 24 / 16 kHz as a
        // bar, green 16-bit, red 8-bit, LED 4 amber (blinking = WRITE would
        // change something). Here: the sweep and the bar on TIME (1 2 4),
        // TIME 8 is the page marker, BANK A = 16-bit, B = 8-bit.
        const bool eight = QualityEightBit(buffer_cursor_);
        const int lit = QualityDecimation(buffer_cursor_);
        const uint8_t depth_led = eight ? 1 : 0;
        if (applied_until_) {
          if (fast_blink) {
            leds_.set_group(LED_TIME, 255);
            leds_.set(LED_BANK, depth_led, 255);
          }
          break;
        }
        const uint32_t age = now - page_opened_;
        if (age < 320) {
          leds_.set(LED_TIME, static_cast<uint8_t>(age / 80), 255);
          break;
        }
        for (int i = 0; i < lit; ++i) leds_.set(LED_TIME, i, 255);
        leds_.set(LED_BANK, depth_led, 255);
        if (buffer_cursor_ != settings_->data().buffer_quality) {
          if (blink) leds_.set(LED_TIME, 3, 255);
        } else {
          leds_.set(LED_TIME, 3, 255);
        }
      }
      break;

    case UI_PAGE_FFT:
      {
        // Monsoon: a green sweep back from LED 4, then the size as a bar
        // (1 = 256 ... 4 = 2048), amber = one size for all, green = each
        // mode's own. Here: the sweep and the bar on TIME, BANK A = each
        // mode's own (green), A + B = one size for all (amber).
        const bool own = fft_cursor_ == 0;
        if (applied_until_) {
          if (fast_blink) {
            leds_.set_group(LED_TIME, 255);
            leds_.set(LED_BANK, 0, 255);
            if (!own) leds_.set(LED_BANK, 1, 255);
          }
          break;
        }
        const uint32_t age = now - page_opened_;
        if (age < 320) {
          leds_.set(LED_TIME, static_cast<uint8_t>(3 - age / 80), 255);
          break;
        }
        size_t n = own ? engine_->native_fft_size() : FftSettingSize(fft_cursor_);
        if (n == 0) {
          leds_.set_group(LED_TIME, 60);
          leds_.set(LED_BANK, 0, 60);
          break;
        }
        int lit = 0;
        for (; n >= 256 && lit < 4; n >>= 1) ++lit;
        const bool pending = fft_cursor_ != settings_->data().fft_setting;
        if (!pending || blink) {
          for (int i = 0; i < lit; ++i) leds_.set(LED_TIME, i, 255);
          leds_.set(LED_BANK, 0, 255);
          if (!own) leds_.set(LED_BANK, 1, 255);
        }
      }
      break;

    case UI_PAGE_CALIBRATION_1:
      // As the stock firmware: the IN row blinks (send 1 V).
      leds_.set_group(LED_IN, blink ? 255 : 0);
      break;

    case UI_PAGE_CALIBRATION_2:
      // The IN and OUT rows blink (send 3 V).
      leds_.set_group(LED_IN, blink ? 255 : 0);
      leds_.set_group(LED_OUT, blink ? 255 : 0);
      break;

    case UI_PAGE_PANIC:
      // Monsoon: all four red. Here: everything lit.
      for (uint8_t g = 0; g < kNumLedGroups; ++g) leds_.set_group(g, 255);
      break;
  }
  if (page != UI_PAGE_SPLASH) leds_.set_freeze(cv_scaler_->freeze());
  leds_.Write();
}

}  // namespace monsoon
