#include "monsoon/cv_scaler.h"

#include <algorithm>

namespace monsoon {

using namespace std;

// Same table as the Monsoon build of Clouds (boourns/eurorack).
const CvScaler::Transformation CvScaler::transformations_[ADC_CHANNEL_LAST] = {
  { true, false, 0.05f },   // POSITION pot+CV
  { true, false, 0.01f },   // DENSITY pot+CV
  { false, false, 0.01f },  // SIZE pot
  { false, true, 0.1f },    // SIZE CV
  { false, false, 0.01f },  // PITCH pot
  { false, false, 1.00f },  // V/OCT
  { false, false, 0.05f },  // WET pot
  { false, true, 0.2f },    // WET CV
  { true, false, 0.01f },   // STEREO pot+CV
  { true, false, 0.01f },   // FEEDBACK pot+CV
  { true, false, 0.01f },   // REVERB pot+CV
  { false, false, 0.01f },  // TEXTURE pot
  { false, true, 0.01f },   // TEXTURE CV
};

// TUNE knob: -48..+48 semitones with gentle "magnetic" plateaus on each
// semitone, so it is easy to land on exact intervals.
static float KnobToSemitones(float k) {
  float x = (k - 0.5f) * 96.0f;
  float n = floorf(x + 0.5f);
  float f = x - n;  // -0.5..0.5
  return n + 4.0f * f * f * f;
}

void CvScaler::Init(CalibrationData* calibration_data) {
  adc_.Init();
  gate_input_.Init();
  calibration_data_ = calibration_data;
  fill(&smoothed_[0], &smoothed_[ADC_CHANNEL_LAST], 0.0f);
  note_ = 0.0f;
  cv_c1_ = 0.0f;
  held_position_ = 0.0f;
  hold_position_ = false;
  freeze_button_ = false;
  freeze_ = false;
  fill(&previous_trigger_[0], &previous_trigger_[kAdcLatency], false);
  fill(&previous_gate_[0], &previous_gate_[kAdcLatency], false);
  memset(&last_, 0, sizeof(last_));
}

void CvScaler::Read(Controls* c) {
  for (size_t i = 0; i < ADC_CHANNEL_LAST; ++i) {
    const Transformation& t = transformations_[i];
    float value = adc_.float_value(i);
    if (t.flip) value = 1.0f - value;
    if (t.remove_offset) value -= calibration_data_->offset[i];
    smoothed_[i] += t.filter_coefficient * (value - smoothed_[i]);
  }

  float position = Clamp(smoothed_[ADC_POSITION_POTENTIOMETER_CV], 0.0f, 1.0f);
  if (hold_position_) {
    position = held_position_;
  } else {
    held_position_ = position;
  }
  c->position = position;
  c->density = Clamp(smoothed_[ADC_DENSITY_POTENTIOMETER_CV], 0.0f, 1.0f);
  c->size = Clamp(smoothed_[ADC_SIZE_POTENTIOMETER] - smoothed_[ADC_SIZE_CV],
                  0.0f, 1.0f);
  c->texture = Clamp(smoothed_[ADC_TEXTURE_POTENTIOMETER] -
                     smoothed_[ADC_TEXTURE_CV] * 2.0f, 0.0f, 1.0f);
  c->blend = Clamp(smoothed_[ADC_WET_POTENTIOMETER] - smoothed_[ADC_WET_CV],
                   0.0f, 1.0f);
  c->spread = Clamp(smoothed_[ADC_STEREO_POTENTIOMETER_CV], 0.0f, 1.0f);
  c->feedback = Clamp(smoothed_[ADC_FEEDBACK_POTENTIOMETER_CV], 0.0f, 1.0f);
  c->reverb = Clamp(smoothed_[ADC_REVERB_POTENTIOMETER_CV], 0.0f, 1.0f);

  float note = calibration_data_->pitch_offset +
      smoothed_[ADC_V_OCT_CV] * calibration_data_->pitch_scale;
  if (fabsf(note - note_) > 0.5f) {
    note_ = note;
  } else {
    note_ += 0.2f * (note - note_);
  }
  c->pitch = Clamp(KnobToSemitones(smoothed_[ADC_PITCH_POTENTIOMETER]) + note_,
                   -48.0f, 48.0f);

  // FREEZE gate inverts the button state, so both work together.
  gate_input_.Read();
  freeze_ = freeze_button_ != gate_input_.freeze();
  c->freeze = freeze_;

  // TRIG is delayed to line up with the (slower) ADC readings.
  c->trigger = previous_trigger_[0];
  c->gate = previous_gate_[0];
  for (int i = 0; i < kAdcLatency - 1; ++i) {
    previous_trigger_[i] = previous_trigger_[i + 1];
    previous_gate_[i] = previous_gate_[i + 1];
  }
  previous_trigger_[kAdcLatency - 1] = gate_input_.trigger_rising_edge();
  previous_gate_[kAdcLatency - 1] = gate_input_.gate();

  last_ = *c;
  adc_.Convert();
}

}  // namespace monsoon
