// Reads pots, CVs and gates and turns them into monsoon::Controls.
// Channel order, flips and offsets follow Tom Burns' Monsoon port of the
// Clouds firmware, so the panel behaves exactly as with stock firmware.

#ifndef MONSOON_CV_SCALER_H_
#define MONSOON_CV_SCALER_H_

#include "stmlib/stmlib.h"

#include "monsoon/drivers/adc.h"
#include "monsoon/drivers/gate_input.h"
#include "monsoon/dsp/core.h"
#include "monsoon/settings.h"

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
  float raw_position() const { return smoothed_[ADC_POSITION_POTENTIOMETER_CV]; }

  void ToggleFreezeButton() { freeze_button_ = !freeze_button_; }
  bool freeze() const { return freeze_; }

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

  // Output-section values (for the DISP page).
  float wet() const { return last_.blend; }
  float stereo() const { return last_.spread; }
  float feedback() const { return last_.feedback; }
  float reverb() const { return last_.reverb; }

 private:
  struct Transformation {
    bool flip;
    bool remove_offset;
    float filter_coefficient;
  };
  static const int kAdcLatency = 5;
  static const Transformation transformations_[ADC_CHANNEL_LAST];

  Adc adc_;
  GateInput gate_input_;
  CalibrationData* calibration_data_;
  float smoothed_[ADC_CHANNEL_LAST];
  float note_;
  float cv_c1_;
  float held_position_;
  bool hold_position_;
  bool freeze_button_;
  bool freeze_;
  bool previous_trigger_[kAdcLatency];
  bool previous_gate_[kAdcLatency];
  Controls last_;

  DISALLOW_COPY_AND_ASSIGN(CvScaler);
};

}  // namespace monsoon

#endif  // MONSOON_CV_SCALER_H_
