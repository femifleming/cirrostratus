// Mode 24 - CHOIR (see choir.h).

#include "monsoon/dsp/modes/choir.h"

namespace monsoon {

// Alto formants: frequency (Hz), level (dB), bandwidth (Hz).
static const float kVowelF[5][ChoirMode::kFormants] = {
  { 800.0f, 1150.0f, 2800.0f, 3500.0f, 4950.0f },   // a
  { 400.0f, 1600.0f, 2700.0f, 3300.0f, 4950.0f },   // e
  { 350.0f, 1700.0f, 2700.0f, 3700.0f, 4950.0f },   // i
  { 450.0f, 800.0f, 2830.0f, 3500.0f, 4950.0f },    // o
  { 325.0f, 700.0f, 2530.0f, 3500.0f, 4950.0f },    // u
};
static const float kVowelDb[5][ChoirMode::kFormants] = {
  { 0.0f, -4.0f, -20.0f, -36.0f, -60.0f },
  { 0.0f, -24.0f, -30.0f, -35.0f, -60.0f },
  { 0.0f, -20.0f, -30.0f, -36.0f, -60.0f },
  { 0.0f, -9.0f, -16.0f, -28.0f, -55.0f },
  { 0.0f, -12.0f, -30.0f, -40.0f, -64.0f },
};
static const float kVowelBw[5][ChoirMode::kFormants] = {
  { 80.0f, 90.0f, 120.0f, 130.0f, 140.0f },
  { 60.0f, 80.0f, 120.0f, 150.0f, 200.0f },
  { 50.0f, 100.0f, 120.0f, 150.0f, 200.0f },
  { 70.0f, 80.0f, 100.0f, 130.0f, 135.0f },
  { 50.0f, 60.0f, 170.0f, 180.0f, 200.0f },
};

static const int kChords = 6;
static const float kChord[kChords][ChoirMode::kVoices] = {
  { 0.0f, 0.0f, 0.0f, 0.0f },     // unison
  { 0.0f, 7.0f, 12.0f, 19.0f },   // fifths
  { 0.0f, 4.0f, 7.0f, 12.0f },    // major
  { 0.0f, 3.0f, 7.0f, 12.0f },    // minor
  { 0.0f, 5.0f, 7.0f, 12.0f },    // sus4
  { 0.0f, 4.0f, 7.0f, 10.0f },    // dominant 7th
};
static const float kSpreadPattern[ChoirMode::kVoices] = {
  -1.0f, 0.33f, -0.33f, 1.0f
};

static const float kOpenQuotient = 0.6f;

bool ChoirMode::Init(Arena* arena) {
  for (int v = 0; v < kVoices; ++v) {
    voice_[v].comb = arena->Allocate<float>(kCombSize);
    if (!voice_[v].comb) return false;
    voice_[v].phase = 0.25f * v;
    voice_[v].vib_phase = 0.31f * v;
    voice_[v].vib_rate = (4.6f + 0.37f * v) / kSampleRate;
    voice_[v].damp = 0.0f;
    voice_hz_[v] = 130.81f;
    pan_[v] = 0.0f;
  }
  for (int ch = 0; ch < 2; ++ch) {
    for (int f = 0; f < kFormants; ++f) formant_[ch][f].Init();
  }
  last_position_ = -1.0f;
  last_bal_ = 0.0f;
  write_ = 0;
  inversion_ = 0;
  frozen_ = false;
  env_.Init(0.01f, 0.25f);
  glottal_level_ = 0.0f;
  rng_.Seed(0xC401);
  dc_[0].Init();
  dc_[1].Init();
  return true;
}

void ChoirMode::UpdateFormants(float position, float shift) {
  float p = Clamp(position, 0.0f, 1.0f) * 4.0f;
  int i0 = static_cast<int>(p);
  if (i0 > 3) i0 = 3;
  float t = p - i0;
  for (int f = 0; f < kFormants; ++f) {
    float hz = Crossfade(kVowelF[i0][f], kVowelF[i0 + 1][f], t) * shift;
    float bw = Crossfade(kVowelBw[i0][f], kVowelBw[i0 + 1][f], t);
    float db = Crossfade(kVowelDb[i0][f], kVowelDb[i0 + 1][f], t);
    formant_gain_[f] = powf(10.0f, db / 20.0f);
    for (int ch = 0; ch < 2; ++ch) {
      formant_[ch][f].set(hz / kSampleRate, hz / bw);
    }
  }
}

void ChoirMode::Process(const Controls& c, const FloatFrame* in,
                        FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  if (c.trigger) inversion_ = (inversion_ + 1) & 3;
  // Formants (POSITION, BAL) and FBK's comb feedback.
  float comb_scale;
  const float comb_fb = Knobs(c, &comb_scale);

  // Chord with inversion: the lowest `inversion_` notes go up an octave.
  int chord = static_cast<int>(Clamp(c.density, 0.0f, 0.999f) * kChords);
  float notes[kVoices];
  for (int v = 0; v < kVoices; ++v) notes[v] = kChord[chord][v];
  for (int v = 0; v < inversion_; ++v) notes[v] += 12.0f;

  const float size = Clamp(c.size, 0.0f, 1.0f);
  const float root = 130.81f * SemitonesToRatio(c.pitch);
  float inc[kVoices], delay[kVoices];
  for (int v = 0; v < kVoices; ++v) {
    float cents = kSpreadPattern[v] * 20.0f * size;
    float hz = Clamp(root * SemitonesToRatio(notes[v] + cents * 0.01f),
                     30.0f, 2000.0f);
    voice_hz_[v] = hz;
    inc[v] = hz / kSampleRate;
    // The damping one-pole in the loop adds ~1 sample.
    delay[v] = Clamp(kSampleRate / hz - 1.0f, 4.0f,
                     static_cast<float>(kCombSize - 4));
    pan_[v] = kSpreadPattern[v] * size;
  }
  const float vib_depth = 0.002f + 0.006f * size;

  const float tex = Clamp(c.texture, 0.0f, 1.0f);
  const float comb_mix = (1.0f - tex) * comb_scale;
  const float glottal_mix = tex;
  const float breath = tex * tex * 0.08f;

  for (size_t s = 0; s < n; ++s) {
    const float x = 0.5f * (in[s].l + in[s].r);
    float e = env_.Process(x);
    float target = frozen_ ? 0.5f : Clamp(e * 3.0f, 0.0f, 0.8f);
    glottal_level_ += 0.002f * (target - glottal_level_);
    const float comb_in = frozen_ ? 0.0f : x;

    float l = 0.0f, r = 0.0f;
    for (int v = 0; v < kVoices; ++v) {
      Voice& vo = voice_[v];
      // Glottal pulse with vibrato.
      vo.vib_phase += vo.vib_rate;
      if (vo.vib_phase >= 1.0f) vo.vib_phase -= 1.0f;
      vo.phase += inc[v] * (1.0f + vib_depth * FastSin(vo.vib_phase));
      if (vo.phase >= 1.0f) vo.phase -= 1.0f;
      float g = 0.0f;
      if (vo.phase < kOpenQuotient) {
        g = FastSin(vo.phase * (0.5f / kOpenQuotient));
        g = g * g - 0.3f;
      } else {
        g = -0.3f;
      }
      float glottal = (g + breath * rng_.Bipolar()) * glottal_level_;

      // Input through a comb tuned to the note.
      float pos = static_cast<float>(write_) - delay[v];
      int32_t ip = static_cast<int32_t>(floorf(pos));
      float fr = pos - static_cast<float>(ip);
      float a = vo.comb[ip & kCombMask];
      float b = vo.comb[(ip + 1) & kCombMask];
      float d = a + (b - a) * fr;
      vo.damp += 0.5f * (d - vo.damp);
      vo.comb[write_] = comb_in + comb_fb * vo.damp;
      float voiced = vo.damp * 0.07f * comb_mix + glottal * glottal_mix;

      l += voiced * (1.0f - pan_[v]);
      r += voiced * (1.0f + pan_[v]);
    }
    write_ = (write_ + 1) & kCombMask;

    float fl = 0.0f, fr2 = 0.0f;
    for (int f = 0; f < kFormants; ++f) {
      fl += formant_gain_[f] * formant_[0][f].BpNormalized(l);
      fr2 += formant_gain_[f] * formant_[1][f].BpNormalized(r);
    }
    out[s].l = dc_[0].Process(fl * 0.9f);
    out[s].r = dc_[1].Process(fr2 * 0.9f);
  }
}

// Updates the formants when POSITION or BAL moved. BAL shifts them (the
// singers' size) by 2^(0.4 BAL), x0.76 .. x1.32, without moving the notes
// (exactly back to v1.0 at noon).
// FBK: the comb feedback 0.93 (v1.0) -> 0.995 is returned; the comb output
// is scaled by (1 - feedback) / 0.07 so a note's peak level stays put
// while it rings longer.
float ChoirMode::Knobs(const Controls& c, float* comb_scale) {
  const float bal = KnobBal(c.bal);
  if (fabsf(c.position - last_position_) > 0.002f ||
      fabsf(bal - last_bal_) > (bal == 0.0f ? 0.0f : 0.002f)) {
    last_bal_ = bal;
    last_position_ = c.position;
    UpdateFormants(c.position, exp2f(0.4f * bal));
  }
  *comb_scale = 1.0f;
  if (c.fbk <= 0.0f) return 0.93f;
  const float fb = 0.93f + 0.065f * (c.fbk < 1.0f ? c.fbk : 1.0f);
  *comb_scale = (1.0f - fb) * (1.0f / 0.07f);
  return fb;
}

}  // namespace monsoon
