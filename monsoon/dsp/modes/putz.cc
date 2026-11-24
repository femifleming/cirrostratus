// Mode 20 — PUTZ: spectral cleaner. See putz.h.

#include "monsoon/dsp/modes/putz.h"

#include "monsoon/dsp/modes/spectral_util.h"

namespace monsoon {

using namespace spectral_util;

bool PutzMode::Init(Arena* arena) {
  if (!stft_.Init(arena, kFftSize, kHop, this)) return false;
  noise_ = arena->Allocate<float>(kBins);
  smooth_ = arena->Allocate<float>(kBins);
  learn_sum_ = arena->Allocate<float>(kBins);
  gain_ = arena->Allocate<float>(kBins);
  win_min_ = arena->Allocate<float>(kBins);
  prev_min_ = arena->Allocate<float>(2 * kBins);   // + tail_ (RVB)
  if (!noise_ || !smooth_ || !learn_sum_ || !gain_ || !win_min_ ||
      !prev_min_) {
    return false;
  }
  for (size_t k = 0; k < kBins; ++k) {
    noise_[k] = 0.0f;
    gain_[k] = 1.0f;
    win_min_[k] = 1e9f;
    prev_min_[k] = 1e9f;
  }
  win_frames_ = 0;
  learn_frames_ = 0;
  learned_ = false;
  learning_ = false;
  prev_energy_ = 0.0f;
  protect_ = 0.0f;
  density_ = size_ = texture_ = 0.0f;
  freeze_ = false;
  listen_ = false;
  prev_trigger_ = false;
  tail_ = prev_min_ + kBins;
  bal_ = rvb_ = 0.0f;
  click_[0] = click_[1] = click_[2] = click_[3] = 0.0f;
  memset(notch_, 0, sizeof(notch_));
  notch_f0_ = 0.0f;
  comb_depth_ = 0.0f;
  return true;
}

void PutzMode::UpdateNotches(float f0) {
  const float q = 12.0f;
  for (int h = 0; h < kHarmonics; ++h) {
    Notch& n = notch_[h];
    float f = f0 * static_cast<float>(h + 1);
    if (f > 20000.0f) f = 20000.0f;
    const float w = kTwoPi * f / kSampleRate;
    const float al = sinf(w) / (2.0f * q);
    const float norm = 1.0f / (1.0f + al);
    n.b0 = norm;
    n.b1 = -2.0f * cosf(w) * norm;
    n.a1 = n.b1;
    n.a2 = (1.0f - al) * norm;
  }
  notch_f0_ = f0;
}

void PutzMode::Process(const Controls& c, const FloatFrame* in,
                       FloatFrame* out, size_t n) {
  density_ = c.density;
  size_ = c.size;
  texture_ = c.texture;
  freeze_ = c.freeze;
  if (c.trigger && !prev_trigger_) listen_ = !listen_;
  prev_trigger_ = c.trigger;
  FloatFrame clean[kBlockSize];
  in = Knobs(c, in, clean, n);

  stft_.Process(in, out, n);

  // Hum comb: 8 notches on the mains fundamental and its harmonics.
  const float base = c.option == 1 ? 60.0f : 50.0f;
  const float f0 = base * exp2f(c.pitch * (1.0f / 48.0f));
  if (fabsf(f0 - notch_f0_) > 0.01f) UpdateNotches(f0);
  const float depth_target = c.position;
  const float depth_inc = (depth_target - comb_depth_) / static_cast<float>(n);
  if (comb_depth_ < 1e-4f && depth_target < 1e-4f) return;
  for (size_t i = 0; i < n; ++i) {
    comb_depth_ += depth_inc;
    float io[2] = { out[i].l, out[i].r };
    for (int ch = 0; ch < 2; ++ch) {
      float x = io[ch];
      float y = x;
      for (int h = 0; h < kHarmonics; ++h) {
        Notch& nt = notch_[h];
        const float o = nt.b0 * (y + nt.x2[ch]) + nt.b1 * nt.x1[ch] -
            nt.a1 * nt.y1[ch] - nt.a2 * nt.y2[ch];
        nt.x2[ch] = nt.x1[ch];
        nt.x1[ch] = y;
        nt.y2[ch] = nt.y1[ch];
        nt.y1[ch] = o;
        y = o;
      }
      io[ch] = x + comb_depth_ * (y - x);
    }
    out[i].l = io[0];
    out[i].r = io[1];
  }
}

void PutzMode::Transform(float* spectrum[2], size_t fft_size) {
  (void)fft_size;
  const size_t h = kBins;
  float* re0 = spectrum[0];
  float* im0 = spectrum[0] + h;
  float* re1 = spectrum[1];
  float* im1 = spectrum[1] + h;

  // FREEZE held: learn the noise profile (the mean power of every bin).
  const bool freeze = freeze_;
  if (freeze && !learning_) {
    learning_ = true;
    learn_frames_ = 0;
    memset(learn_sum_, 0, h * sizeof(float));
  }
  if (!freeze && learning_) {
    learning_ = false;
    if (learn_frames_ >= 4) {
      const float inv = 1.0f / static_cast<float>(learn_frames_);
      for (size_t k = 1; k < h; ++k) noise_[k] = learn_sum_[k] * inv;
      learned_ = true;
    }
  }

  const float density = density_;
  const float size = size_;
  const float alpha = 1.0f + 2.0f * density;
  const float gmin = FastExp2(-density * 4.98f);       // 0 .. -30 dB
  const float gmin2 = gmin * gmin;
  const float beta = 1.0f - 0.85f * size;              // gain smoothing
  const float beta_up = beta * 2.0f > 1.0f ? 1.0f : beta * 2.0f;
  const bool knobs = bal_ != 0.0f || rvb_ > 0.001f;
  // Minimum statistics: the noise is the lowest smoothed power seen over
  // the current and the previous ~1 s window (94 hops), so the estimate
  // follows a rising noise floor within 2 s and ignores speech/music.
  const bool new_window = ++win_frames_ >= 94;
  if (new_window) win_frames_ = 0;

  float energy = 0.0f;
  for (size_t k = 1; k < h; ++k) {
    const float p = re0[k] * re0[k] + im0[k] * im0[k] +
        re1[k] * re1[k] + im1[k] * im1[k];
    energy += p;
    smooth_[k] += 0.27f * (p - smooth_[k]);
    if (learning_) {
      learn_sum_[k] += p;
    } else if (!learned_) {
      if (smooth_[k] < win_min_[k]) win_min_[k] = smooth_[k];
      const float mn = win_min_[k] < prev_min_[k] ? win_min_[k] : prev_min_[k];
      // The minimum of a smoothed power underestimates its mean (~2.5 dB).
      noise_[k] = mn * 1.8f;
      if (new_window) {
        prev_min_[k] = win_min_[k];
        win_min_[k] = 1e9f;
      }
    }
    // RVB's tail follower: the smoothed power's peak, falling 60 dB in
    // ~4 s.
    const float tail = tail_[k] * 0.963f;
    tail_[k] = smooth_[k] > tail ? smooth_[k] : tail;
    // Power subtraction, as a magnitude gain, with a floor.
    float g = 1.0f;
    if (knobs) {
      g = sqrtf(KnobPower(k, p, density > 0.001f ? alpha : 0.0f, gmin2));
    } else if (density > 0.001f) {
      float r = 1.0f - alpha * noise_[k] / (p + 1e-24f);
      if (r < gmin2) r = gmin2;
      g = sqrtf(r);
    }
    float& s = gain_[k];
    s += (g > s ? beta_up : beta) * (g - s);
  }
  if (learning_) ++learn_frames_;

  // Transient protection: a sudden jump in energy opens the gate briefly.
  if (energy > 4.0f * prev_energy_ + 1e-12f) {
    protect_ = texture_;
  } else {
    protect_ *= 0.5f;
  }
  prev_energy_ = energy;

  // Apply, smoothed across neighbouring bins by SIZE.
  const float side = 0.25f * size;
  const float centre = 1.0f - 2.0f * side;
  const bool listen = listen_;
  const float protect = protect_;
  float prev = gain_[1];
  for (size_t k = 1; k < h; ++k) {
    const float next = k + 1 < h ? gain_[k + 1] : gain_[k];
    float g = centre * gain_[k] + side * (prev + next);
    prev = gain_[k];
    if (g < protect) g = protect;
    if (listen) g = 1.0f - g;
    re0[k] *= g; im0[k] *= g;
    re1[k] *= g; im1[k] *= g;
  }
}

// BAL and RVB are read by Transform; FBK de-crackles the input into
// `clean` (returned instead of `in` while it is on).
const FloatFrame* PutzMode::Knobs(const Controls& c, const FloatFrame* in,
                                  FloatFrame* clean, size_t n) {
  bal_ = KnobBal(c.bal);
  rvb_ = c.rvb > 1.0f ? 1.0f : c.rvb;
  if (c.fbk <= 0.002f || n > kBlockSize) return in;
  // De-crackle: what a one-pole low-pass (~2.5 kHz, following the cleaned
  // signal) does not predict is clamped to a multiple of its own level
  // (fast-ish attack, slow release), so a click lasting a few samples is
  // flattened and steady sound is not. Threshold 28x (FBK near 0) -> 3x
  // (full).
  const float k = 1.0f - (c.fbk > 1.0f ? 1.0f : c.fbk);
  const float crest = 3.0f + 25.0f * k * k;
  for (int ch = 0; ch < 2; ++ch) {
    const float* x = &in[0].l + ch;
    float* y = &clean[0].l + ch;
    float lp = click_[2 * ch], env = click_[2 * ch + 1];
    for (size_t i = 0; i < n; ++i, x += 2, y += 2) {
      const float h = *x - lp;
      const float a = fabsf(h);
      env += (a > env ? 0.004f : 0.0004f) * (a - env);
      const float lim = crest * env + 1e-4f;
      const float hc = h > lim ? lim : (h < -lim ? -lim : h);
      *y = lp + hc;
      lp += 0.28f * hc;   // the low-pass follows the cleaned signal
    }
    click_[2 * ch] = lp;
    click_[2 * ch + 1] = env;
  }
  return clean;
}

// The power gain of bin k when BAL or RVB is in use.
// BAL tilts the over-subtraction across the spectrum (weight 0..2 on a
// fourth-root frequency scale, pivot ~1.5 kHz: the lows or the highs are
// cleaned twice as hard, the other end is left alone).
// RVB (de-reverb): the bin's power below its tail follower is pushed
// down by (power / tail)^(2 RVB) (a blend of 1, the ratio and its square),
// so falling energy falls up to 3x faster (in dB), at most -24 dB.
float PutzMode::KnobPower(size_t k, float p, float alpha, float gmin2) {
  const float x = sqrtf(sqrtf(static_cast<float>(k) * (1.0f / kBins)));
  const float a = alpha * (1.0f + bal_ * (2.0f * x - 1.0f));
  float r = 1.0f - a * noise_[k] / (p + 1e-24f);
  if (r < gmin2) r = gmin2;
  const float rv = 2.0f * rvb_;
  if (rv > 0.002f && smooth_[k] < tail_[k]) {
    const float q = smooth_[k] / tail_[k];
    float d = rv < 1.0f ? 1.0f + rv * (q - 1.0f)
                        : q * (1.0f + (rv - 1.0f) * (q - 1.0f));
    r *= d > 0.004f ? d : 0.004f;
  }
  return r;
}

}  // namespace monsoon
