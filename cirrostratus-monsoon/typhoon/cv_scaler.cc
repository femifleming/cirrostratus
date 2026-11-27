#include "typhoon/cv_scaler.h"

#include <algorithm>

namespace monsoon {

using namespace std;

// One-pole smoothing per audio block (1.5 kHz), chosen to match the Monsoon
// build's response for the same control (its pots and CVs are partly summed
// in hardware, so these follow the summed channel where there is one).
static const float kCvFilter[ADC_CHANNEL_LAST] = {
  0.05f,  // POSITION CV
  0.01f,  // DENSITY CV
  0.01f,  // SIZE slider (read directly on ADC1)
  0.1f,   // SIZE CV
  0.9f,   // FREQ CV
  0.01f,  // STEREO CV
  0.01f,  // FEEDBACK CV
  0.01f,  // REVERB CV
  0.2f,   // WET CV
  0.01f,  // TEXTURE CV
  1.0f,   // V/OCT
  0.1f,   // OUT level sense
};

static const float kPotFilter[POT_LAST] = {
  0.01f,  // STEREO
  0.01f,  // FEEDBACK
  0.01f,  // REVERB
  0.05f,  // WET
  0.05f,  // POSITION
  0.01f,  // TEXTURE
  0.01f,  // TUNE
  0.01f,  // DENSITY
};

// TUNE knob: -48..+48 semitones with gentle "magnetic" plateaus on each
// semitone, so it is easy to land on exact intervals (as on the Monsoon).
static float KnobToSemitones(float k) {
  float x = (k - 0.5f) * 96.0f;
  float n = floorf(x + 0.5f);
  float f = x - n;  // -0.5..0.5
  return n + 4.0f * f * f * f;
}

void CvScaler::Init(CalibrationData* calibration_data) {
  adc_.Init();
  pots_.Init();  // after adc_ (ADC1 sets the common ADC prescaler)
  gate_input_.Init();
  calibration_data_ = calibration_data;
  fill(&smoothed_cv_[0], &smoothed_cv_[ADC_CHANNEL_LAST], 0.0f);
  fill(&smoothed_pot_[0], &smoothed_pot_[POT_LAST], 0.0f);
  smoothed_cv_[ADC_VCA_OUT_LEVEL] = 1.0f;  // start with the OUT meter off
  note_ = 0.0f;
  cv_c1_ = 0.0f;
  held_position_ = 0.0f;
  hold_position_ = false;
  freeze_button_ = false;
  freeze_ = false;
  trig_button_ = false;
  previous_trig_ = false;
  fill(&previous_trigger_[0], &previous_trigger_[kAdcLatency], false);
  fill(&previous_gate_[0], &previous_gate_[kAdcLatency], false);
  memset(&last_, 0, sizeof(last_));
}

void CvScaler::Read(Controls* c) {
  pots_.Scan();
  for (size_t i = 0; i < ADC_CHANNEL_LAST; ++i) {
    float value = adc_.float_value(i);
    if (i != ADC_SIZE_POT && i != ADC_V_OCT_CV && i != ADC_VCA_OUT_LEVEL) {
      // A CV jack: inverted by its input stage, so its voltage relative to
      // 0 V is (reading at 0 V - reading), +-0.5 for +-5 V.
      value = calibration_data_->offset[i] - value;
    }
    smoothed_cv_[i] += kCvFilter[i] * (value - smoothed_cv_[i]);
  }
  for (size_t i = 0; i < POT_LAST; ++i) {
    smoothed_pot_[i] += kPotFilter[i] * (pots_.float_value(i) - smoothed_pot_[i]);
  }

  float position = Clamp(smoothed_pot_[POT_POSITION] +
                         2.0f * smoothed_cv_[ADC_POSITION_CV], 0.0f, 1.0f);
  if (hold_position_) {
    position = held_position_;
  } else {
    held_position_ = position;
  }
  c->position = position;
  c->density = Clamp(smoothed_pot_[POT_DENSITY] +
                     2.0f * smoothed_cv_[ADC_DENSITY_CV], 0.0f, 1.0f);
  c->size = Clamp(smoothed_cv_[ADC_SIZE_POT] +
                  2.0f * smoothed_cv_[ADC_SIZE_CV], 0.0f, 1.0f);
  c->texture = Clamp(smoothed_pot_[POT_TEXTURE] +
                     2.0f * smoothed_cv_[ADC_TEXTURE_CV], 0.0f, 1.0f);
  c->blend = Clamp(smoothed_pot_[POT_WET] +
                   2.0f * smoothed_cv_[ADC_WET_CV], 0.0f, 1.0f);
  // STEREO / FEEDBACK / REVERB knobs are the Monsoon's BAL / FBK / RVB.
  c->bal = Clamp(smoothed_pot_[POT_STEREO] +
                 2.0f * smoothed_cv_[ADC_STEREO_CV], 0.0f, 1.0f);
  c->fbk = Clamp(smoothed_pot_[POT_FEEDBACK] +
                 2.0f * smoothed_cv_[ADC_FEEDBACK_CV], 0.0f, 1.0f);
  c->rvb = Clamp(smoothed_pot_[POT_REVERB] +
                 2.0f * smoothed_cv_[ADC_REVERB_CV], 0.0f, 1.0f);

  // V/OCT (calibrated) + FREQ jack (+-5 V = +-12 semitones, as stock).
  float note = calibration_data_->pitch_offset +
      smoothed_cv_[ADC_V_OCT_CV] * calibration_data_->pitch_scale +
      smoothed_cv_[ADC_FREQ_CV] * 24.0f;
  if (fabsf(note - note_) > 0.5f) {
    note_ = note;
  } else {
    note_ += 0.2f * (note - note_);
  }
  c->pitch = Clamp(KnobToSemitones(smoothed_pot_[POT_TUNE]) + note_,
                   -48.0f, 48.0f);

  // HOLD gate inverts the FREEZE button state, so both work together.
  gate_input_.Read();
  freeze_ = freeze_button_ != gate_input_.freeze();
  c->freeze = freeze_;

  // TRIG jack or TRIG button; delayed to line up with the ADC readings.
  bool trig = gate_input_.gate() || trig_button_;
  bool rising = trig && !previous_trig_;
  previous_trig_ = trig;
  c->trigger = previous_trigger_[0];
  c->gate = previous_gate_[0];
  for (int i = 0; i < kAdcLatency - 1; ++i) {
    previous_trigger_[i] = previous_trigger_[i + 1];
    previous_gate_[i] = previous_gate_[i + 1];
  }
  previous_trigger_[kAdcLatency - 1] = rising;
  previous_gate_[kAdcLatency - 1] = trig;

  last_ = *c;
  adc_.Convert();
}

float CvScaler::out_meter_scale() const {
  // The sense voltage swings ~2.5 V (quiet) to 0 V (full level), inverted.
  float level = 1.0f - Clamp(smoothed_cv_[ADC_VCA_OUT_LEVEL], 0.0f, 1.0f);
  float scale = level * level;
  scale = scale * scale * 2.1f;
  return scale < 0.15f ? 0.0f : scale;
}

}  // namespace monsoon
