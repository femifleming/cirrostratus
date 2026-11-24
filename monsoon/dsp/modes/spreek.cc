// Mode 8: SPREEK (see spreek.h).

#include "monsoon/dsp/modes/spreek.h"
#include "monsoon/dsp/modes/cold_code.h"

namespace monsoon {

namespace {

struct Vowel {
  float f[SpreekMode::kFormants];
  float db[SpreekMode::kFormants];
  float bw[SpreekMode::kFormants];
};

// Classic singer formant tables: tenor (adult) and soprano (child, also
// scaled up by 10%). Order: a e i o u.
const Vowel kAdult[5] = {
  { { 650, 1080, 2650, 2900, 3250 }, { 0, -6, -7, -8, -22 }, { 80, 90, 120, 130, 140 } },
  { { 400, 1700, 2600, 3200, 3580 }, { 0, -14, -12, -14, -20 }, { 70, 80, 100, 120, 120 } },
  { { 290, 1870, 2800, 3250, 3540 }, { 0, -15, -18, -20, -30 }, { 40, 90, 100, 120, 120 } },
  { { 400, 800, 2600, 2800, 3000 }, { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 } },
  { { 350, 600, 2700, 2900, 3300 }, { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 } },
};
const Vowel kChild[5] = {
  { { 800, 1150, 2900, 3900, 4950 }, { 0, -6, -32, -20, -50 }, { 80, 90, 120, 130, 140 } },
  { { 350, 2000, 2800, 3600, 4950 }, { 0, -20, -15, -40, -56 }, { 60, 100, 120, 150, 200 } },
  { { 270, 2140, 2950, 3900, 4950 }, { 0, -12, -26, -26, -44 }, { 60, 90, 100, 120, 120 } },
  { { 450, 800, 2830, 3800, 4950 }, { 0, -11, -22, -22, -50 }, { 40, 80, 100, 120, 120 } },
  { { 325, 700, 2700, 3800, 4950 }, { 0, -16, -35, -40, -60 }, { 50, 60, 170, 180, 200 } },
};

const float kPreEmphasis = 0.9f;
const float kSawRms = 0.57735f;

inline float PolyBlep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  } else if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

// One sample of a band-limited saw; returns true in `wrapped` when the
// phase wrapped. Out of line: it serves the carrier and the choir.
__attribute__((noinline)) float BlepSaw(float* phase, float dt, bool* wrapped) {
  float p = *phase + dt;
  *wrapped = p >= 1.0f;
  if (*wrapped) p -= 1.0f;
  *phase = p;
  return 2.0f * p - 1.0f - PolyBlep(p, dt);
}

}  // namespace

MONSOON_COLD bool SpreekMode::Init(Arena* arena) {
  ring_ = arena->Allocate<float>(kWindow);
  frame_ = arena->Allocate<float>(kWindow);
  if (!ring_ || !frame_) return false;
  ring_pos_ = 0;
  hop_count_ = 0;
  frame_pending_ = false;
  coeffs_ready_ = false;
  memset(k_new_, 0, sizeof(k_new_));
  memset(k_, 0, sizeof(k_));
  memset(k_target_, 0, sizeof(k_target_));
  memset(ba_, 0, sizeof(ba_));
  memset(bs_, 0, sizeof(bs_));
  e_new_ = e_ = e_target_ = 0.0f;
  pre_x1_ = de_y1_ = 0.0f;
  for (int i = 0; i < kFormants; ++i) {
    formant_[i].Init();
    f_freq_[i] = kAdult[0].f[i];
    f_amp_[i] = 0.0f;
    f_q_[i] = 5.0f;
  }
  phase_ = 0.0f;
  choir_phase_[0] = 0.31f;
  choir_phase_[1] = 0.67f;
  drift_phase_ = 0.0f;
  vib_phase_ = 0.0f;
  glide_ = 0.0f;
  odd_period_ = false;
  lpc_mix_ = 0.0f;
  env_.Init(0.005f, 0.08f);
  out_env_ = 0.0f;
  agc_ = 1.0f;
  held_env_ = 0.0f;
  held_position_ = 0.5f;
  burst_ = 0.0f;
  burst_filter_.Init();
  rng_.Seed(0x5B2E);
  frozen_ = false;
  return true;
}

// Autocorrelation + Levinson-Durbin -> reflection coefficients.
void SpreekMode::Prepare() {
  if (!frame_pending_) return;
  float r[kOrder + 1];
  float w2 = 0.0f;
  // Window in place (the frame is a private copy).
  for (size_t i = 0; i < kWindow; ++i) {
    float s = FastSin(0.5f * (i + 0.5f) / kWindow);
    float w = s * s;
    frame_[i] *= w;
    w2 += w * w;
  }
  for (int lag = 0; lag <= kOrder; ++lag) {
    float acc = 0.0f;
    for (size_t i = lag; i < kWindow; ++i) acc += frame_[i] * frame_[i - lag];
    r[lag] = acc;
  }
  frame_pending_ = false;

  float k[kOrder];
  float E = r[0] * 1.0001f + 1e-9f;  // tiny white-noise floor
  if (r[0] < 1e-7f) {
    memset(k, 0, sizeof(k));
    E = 0.0f;
  } else {
    // Lag window (mild bandwidth expansion) keeps sharp peaks tame.
    for (int i = 1; i <= kOrder; ++i) {
      float g = 0.0019f * i;
      r[i] *= 1.0f - g * g * 0.5f * 100.0f;
    }
    float a[kOrder + 1] = { 0 };
    float tmp[kOrder + 1];
    for (int i = 1; i <= kOrder; ++i) {
      float acc = r[i];
      for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
      float ki = -acc / E;
      ki = Clamp(ki, -0.995f, 0.995f);
      for (int j = 1; j < i; ++j) tmp[j] = a[j] + ki * a[i - j];
      for (int j = 1; j < i; ++j) a[j] = tmp[j];
      a[i] = ki;
      k[i - 1] = ki;
      E *= 1.0f - ki * ki;
    }
  }
  memcpy(k_new_, k, sizeof(k));
  e_new_ = E / (w2 + 1e-9f);  // residual power per sample
  coeffs_ready_ = true;
}

// FIR lattice A(z): input -> prediction residual.
inline float SpreekMode::Analyse(float x) {
  float f = x;
  float prev = ba_[0];
  ba_[0] = x;
  for (int i = 1; i <= kOrder; ++i) {
    float fn = f + k_[i - 1] * prev;
    float bn = prev + k_[i - 1] * f;
    if (i < kOrder) {
      float next_prev = ba_[i];
      ba_[i] = bn;
      prev = next_prev;
    }
    f = fn;
  }
  return f;
}

// IIR lattice 1/A(z): excitation -> shaped signal.
inline float SpreekMode::Synthesise(float e) {
  float f = e;
  for (int i = kOrder; i >= 1; --i) {
    f -= k_[i - 1] * bs_[i - 1];
    if (i < kOrder) bs_[i] = bs_[i - 1] + k_[i - 1] * f;
  }
  bs_[0] = f;
  return f;
}

void SpreekMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  // --- Freeze: hold the phoneme.
  if (c.freeze && !frozen_) {
    held_env_ = Clamp(env_.value(), 0.08f, 0.6f);
    held_position_ = c.position;
  }
  frozen_ = c.freeze;
  if (coeffs_ready_ && !frozen_) {
    memcpy(k_target_, k_new_, sizeof(k_target_));
    e_target_ = e_new_;
    coeffs_ready_ = false;
  }
  if (c.trigger) burst_ = 1.0f;

  const float position = frozen_ ? held_position_ : Clamp(c.position, 0.0f, 1.0f);
  const float lpc_target = Clamp((0.18f - position) / 0.06f, 0.0f, 1.0f);

  // --- Vowel formants.
  const bool child = c.option == 1;
  const Vowel* table = child ? kChild : kAdult;
  const float scale = 0.7f * exp2f(Clamp(c.size, 0.0f, 1.0f)) * (child ? 1.1f : 1.0f);
  const float texture = Clamp(c.texture, 0.0f, 1.0f);
  const float bw_scale = 1.0f + 1.5f * texture;
  float v = Clamp((position - 0.15f) / 0.85f, 0.0f, 1.0f) * 4.0f;
  int v0 = static_cast<int>(v);
  if (v0 > 3) v0 = 3;
  float vt = v - v0;
  for (int i = 0; i < kFormants; ++i) {
    const Vowel& a = table[v0];
    const Vowel& b = table[v0 + 1];
    float f = a.f[i] * powf(b.f[i] / a.f[i], vt) * scale;
    float db = Crossfade(a.db[i], b.db[i], vt);
    float bw = Crossfade(a.bw[i], b.bw[i], vt) * bw_scale * (0.5f + 0.5f * scale);
    f = Clamp(f, 50.0f, 0.45f * kSampleRate);
    f_freq_[i] += 0.3f * (f - f_freq_[i]);
    f_amp_[i] += 0.3f * (powf(10.0f, db / 20.0f) - f_amp_[i]);
    f_q_[i] = Clamp(f_freq_[i] / bw, 1.0f, 40.0f);
    formant_[i].set(f_freq_[i] / kSampleRate, f_q_[i]);
  }
  burst_filter_.set(Clamp(4200.0f * scale, 500.0f, 15000.0f) / kSampleRate, 1.5f);

  // --- Source.
  const float density = frozen_ ? 1.0f : Clamp(c.density, 0.0f, 1.0f);
  // BAL: glide (left) or vibrato (right) on the carrier's pitch.
  const float bal = KnobBipolar(c.bal);
  float pitch = c.pitch;
  if (bal < 0.0f) {
    const float tau = bal * bal * (0.25f * kSampleRate);  // samples
    glide_ += static_cast<float>(n) / (tau + n) * (c.pitch - glide_);
    pitch = glide_;
  } else {
    glide_ = c.pitch;
    if (bal > 0.0f) {
      vib_phase_ += 5.3f * n / kSampleRate;
      if (vib_phase_ >= 1.0f) vib_phase_ -= 1.0f;
      const float x = 2.0f * vib_phase_ - 1.0f;  // parabolic sine
      pitch += bal * 4.0f * x * (1.0f - fabsf(x));
    }
  }
  const float f0 = Clamp(130.81f * SemitonesToRatio(pitch), 20.0f, 4000.0f);
  const float dt = f0 / kSampleRate;
  const float breath = texture * 0.6f;
  // FBK: growl. Every other glottal period is weaker and longer (and the
  // ones between shorter, so the pitch stays put).
  const float growl = Clamp(c.fbk, 0.0f, 1.0f);
  const float dt_even = dt / (1.0f - 0.08f * growl);
  const float dt_odd = dt / (1.0f + 0.08f * growl);
  const float gain_odd = 1.0f - 0.7f * growl;
  // RVB: choir. Two more carriers, detuned (up to +/-22 cents) and
  // drifting with a slow triangle, out of phase with each other.
  const float choir = Clamp(c.rvb, 0.0f, 1.0f);
  float dt_choir[2];
  drift_phase_ += 0.23f * n / kSampleRate;
  if (drift_phase_ >= 1.0f) drift_phase_ -= 1.0f;
  const float drift = fabsf(2.0f * drift_phase_ - 1.0f);
  const float detune = 0.0128f * choir;
  dt_choir[0] = dt * (1.0f - detune * (0.55f + 0.45f * drift));
  dt_choir[1] = dt * (1.0f + detune * (1.0f - 0.45f * drift));
  const float choir_norm = 1.0f / sqrtf(1.0f + 2.0f * choir * choir);

  for (size_t s = 0; s < n; ++s) {
    float x = 0.5f * (in[s].l + in[s].r);
    env_.Process(x);
    float pe = x - kPreEmphasis * pre_x1_;
    pre_x1_ = x;

    // Analysis ring and hand-off every hop.
    ring_[ring_pos_] = pe;
    if (++ring_pos_ >= kWindow) ring_pos_ = 0;
    if (++hop_count_ >= kHop) {
      hop_count_ = 0;
      if (!frame_pending_) {
        for (size_t i = 0; i < kWindow; ++i) {
          size_t j = ring_pos_ + i;
          if (j >= kWindow) j -= kWindow;
          frame_[i] = ring_[j];
        }
        frame_pending_ = true;
      }
    }

    // Glide the filter.
    for (int i = 0; i < kOrder; ++i) k_[i] += 0.004f * (k_target_[i] - k_[i]);
    e_ += 0.004f * (e_target_ - e_);
    lpc_mix_ += 0.002f * (lpc_target - lpc_mix_);

    // Glottal source: band-limited saw with breath noise.
    bool wrapped;
    float saw = BlepSaw(&phase_, odd_period_ ? dt_odd : dt_even, &wrapped);
    if (wrapped) odd_period_ = !odd_period_;
    if (odd_period_) saw *= gain_odd;
    if (choir > 0.0f) {
      for (int k = 0; k < 2; ++k) {
        saw += choir * BlepSaw(&choir_phase_[k], dt_choir[k], &wrapped);
      }
      saw *= choir_norm;
    }
    float noise = rng_.Bipolar() * 1.7f * kSawRms;
    float carrier = ((1.0f - breath) * saw + breath * noise) / kSawRms;

    float y = 0.0f;
    if (lpc_mix_ > 0.001f) {
      float residual = Analyse(pe);
      float excitation = (1.0f - density) * residual +
          density * carrier * sqrtf(e_ > 0.0f ? e_ : 0.0f);
      float shaped = Synthesise(excitation);
      // De-emphasis.
      de_y1_ = shaped + kPreEmphasis * de_y1_;
      y += lpc_mix_ * de_y1_;
    }
    if (lpc_mix_ < 0.999f) {
      float level = frozen_ ? held_env_ : env_.value();
      float src = (1.0f - density) * x + density * carrier * level * 0.6f;
      float vowel = 0.0f;
      for (int i = 0; i < kFormants; ++i) {
        vowel += f_amp_[i] * formant_[i].BpNormalized(src);
      }
      y += (1.0f - lpc_mix_) * vowel;
    }
    if (burst_ > 0.0005f) {
      y += burst_filter_.BpNormalized(rng_.Bipolar()) * burst_ * 0.5f;
      burst_ *= 0.9985f;
    } else {
      burst_ = 0.0f;
    }

    // Loudness follows the input (or the held level when frozen).
    out_env_ += (fabsf(y) > out_env_ ? 0.01f : 0.0004f) * (fabsf(y) - out_env_);
    y *= agc_;
    if (!(fabsf(y) < 8.0f)) {  // runaway guard: reset the filters
      memset(ba_, 0, sizeof(ba_));
      memset(bs_, 0, sizeof(bs_));
      de_y1_ = 0.0f;
      y = 0.0f;
    }
    out[s].l = y;
    out[s].r = y;
  }
  float target_level = frozen_ ? held_env_ : env_.value();
  float target = target_level / (out_env_ + 1e-4f);
  target = Clamp(target, 0.1f, 8.0f);
  if (target_level < 1e-4f && burst_ == 0.0f) target = agc_;
  agc_ += 0.02f * (target - agc_);
}

}  // namespace monsoon
