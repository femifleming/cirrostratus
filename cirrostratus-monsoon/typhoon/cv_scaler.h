// Reads the Typhoon's pots, CVs and gates and turns them into
// monsoon::Controls (the same engine controls as the Monsoon build).
//
// Unlike the Monsoon, every slider/knob and its CV jack are read
// separately and summed here, as the stock firmware (SuperParasites,
// Microcell variant) does: control = pot + 2 x CV, where CV is the jack's
// voltage relative to its calibrated 0 V reading, so +-5 V spans the whole
// range. The CV stages invert (gain -0.33, +1.65 V bias), so the CVs are
// flipped.

#ifndef TYPHOON_CV_SCALER_H_
#define TYPHOON_CV_SCALER_H_

#include "stmlib/stmlib.h"

#include "typhoon/drivers/adc.h"
#include "typhoon/drivers/gate_input.h"
#include "typhoon/drivers/pots_adc.h"
#include "monsoon/dsp/core.h"
#include "typhoon/settings.h"

namespace monsoon {

class CvScaler {
 public:
  CvScaler() { }
  ~CvScaler() { }

  void Init(CalibrationData* calibration_data);
  void Read(Controls* controls);

  // While the mode menu is open, POSITION picks a mode: the sound keeps the
  // last position value instead of following the slider.
  void set_hold_position(bool hold) { hold_position_ = hold; }
  // The POSITION slider alone (not its CV): what the mode menu reads.
  float raw_position() const { return smoothed_pot_[POT_POSITION]; }

  void ToggleFreezeButton() { freeze_button_ = !freeze_button_; }
  bool freeze() const { return freeze_; }

  // The TRIG button (from the UI's debounced state) acts like the TRIG jack.
  void set_trig_button(bool held) { trig_button_ = held; }

  void CalibrateC1() { cv_c1_ = adc_.float_value(ADC_V_OCT_CV); }
  void CalibrateOffsets() {
    for (size_t i = 0; i < ADC_CHANNEL_LAST; ++i) {
      calibration_data_->offset[i] = adc_.float_value(i);
    }
  }
  bool CalibrateC3() {
    float c3 = adc_.float_value(ADC_V_OCT_CV);
    float c1 = cv_c1_;
    float delta = c3 - c1;
    if (delta > -0.5f && delta < -0.0f) {
      calibration_data_->pitch_scale = 24.0f / (c3 - c1);
      calibration_data_->pitch_offset = 12.0f -
          calibration_data_->pitch_scale * c1;
      return true;
    }
    return false;
  }

  // Knob values for the DISP page: WET and the three per-mode knobs.
  float wet() const { return last_.blend; }
  float bal() const { return last_.bal; }
  float fbk() const { return last_.fbk; }
  float rvb() const { return last_.rvb; }

  // How loud the module's analog output is relative to the codec's, from
  // the OUT LEVEL / OUT VCA control voltage (PC4). Same curve as the stock
  // firmware (its OUT meter: level^4 x 2.1, nothing below 0.15).
  float out_meter_scale() const;

 private:
  static const int kAdcLatency = 5;

  Adc adc_;
  PotsAdc pots_;
  GateInput gate_input_;
  CalibrationData* calibration_data_;
  float smoothed_cv_[ADC_CHANNEL_LAST];
  float smoothed_pot_[POT_LAST];
  float note_;
  float cv_c1_;
  float held_position_;
  bool hold_position_;
  bool freeze_button_;
  bool freeze_;
  volatile bool trig_button_;
  bool previous_trig_;
  bool previous_trigger_[kAdcLatency];
  bool previous_gate_[kAdcLatency];
  Controls last_;

  DISALLOW_COPY_AND_ASSIGN(CvScaler);
};

}  // namespace monsoon

#endif  // TYPHOON_CV_SCALER_H_
