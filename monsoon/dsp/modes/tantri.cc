// Mode 18 - TANTRI (see tantri.h).

#include "monsoon/dsp/modes/tantri.h"

namespace monsoon {

// ---------------------------------------------------------------------------
// Tabla data (from Tantri's Tabla.h, loaded/syahi = 1 ratios).

// Dayan: Raman's harmonic groups; the (1,1) and (2,1) modes appear twice
// (the split degenerate pair that makes a real head beat).
static const float kDayanRatio[TantriMode::kDayanModes] = {
  1.000f, 2.000f, 2.007f, 3.000f, 3.0105f, 3.010f, 4.000f, 4.020f,
  5.000f, 5.020f, 5.050f, 5.830f, 6.400f, 7.300f
};
static const float kDayanT60[TantriMode::kDayanModes] = {
  1.6f, 1.3f, 1.3f, 1.0f, 1.0f, 0.9f, 0.75f, 0.7f,
  0.5f, 0.45f, 0.45f, 0.16f, 0.14f, 0.12f
};
static const uint8_t kDayanGroup[TantriMode::kDayanModes] = {
  0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5
};
static const float kBayanRatio[TantriMode::kBayanModes] = {
  1.0f, 1.52f, 2.0f, 2.18f, 2.65f, 2.9f, 3.2f, 3.6f
};
static const float kBayanT60[TantriMode::kBayanModes] = {
  1.1f, 0.8f, 0.55f, 0.45f, 0.35f, 0.3f, 0.25f, 0.2f
};
static const uint8_t kBayanGroup[TantriMode::kBayanModes] = {
  0, 1, 2, 2, 3, 3, 4, 4
};

// Per-stroke drive of every mode = group excitation x Bessel radial shape
// at the stroke's radius (x pair gain), precomputed from Tantri's tables.
static const float kDayanDrive[4][TantriMode::kDayanModes] = {
  { 0.0108f, 0.1903f, 0.1142f, 0.2174f, 0.1304f, -0.1134f, 0.1861f, -0.1516f,
    0.1790f, -0.1615f, 0.0808f, 0.2805f, -0.2655f, 0.3082f },   // Na
  { 0.2844f, 0.9028f, 0.5417f, 0.5477f, 0.3286f, -0.2032f, 0.3945f, -0.1291f,
    0.2777f, -0.0173f, -0.0161f, 0.2941f, 0.0800f, 0.2612f },   // Tin
  { 0.9914f, 0.0651f, 0.0390f, 0.0128f, 0.0077f, 0.2865f, 0.0008f, 0.0557f,
    0.0001f, 0.0112f, 0.0891f, 0.0000f, 0.0011f, 0.0000f },     // Tun
  { 0.7747f, 0.1658f, 0.0995f, 0.0581f, 0.0348f, 0.3354f, 0.0099f, 0.1964f,
    0.0021f, 0.0717f, 0.1252f, 0.0010f, 0.0526f, 0.0002f },     // Te
};
static const float kBayanDrive[2][TantriMode::kBayanModes] = {
  { 0.3800f, 0.4000f, 0.3087f, -0.1400f, 0.2405f, -0.1432f, 0.1793f, -0.0942f },  // Ge
  { 0.4371f, 0.4478f, 0.2994f, -0.0142f, 0.1975f, 0.1438f, 0.1198f, 0.2119f },    // Ke
};
// Damping left on the head by closed strokes (extra T60 per group, s).
static const float kNoDamp[6] = { 0, 0, 0, 0, 0, 0 };
static const float kNaDamp[6] = { 0.04f, 0, 0, 0, 0, 0 };
static const float kTeDamp[6] = { 0.045f, 0.04f, 0.035f, 0.03f, 0.03f, 0.02f };
static const float kKeDamp[6] = { 0.035f, 0.03f, 0.025f, 0.025f, 0.02f, 0.02f };

// ---------------------------------------------------------------------------
// Tanpura bridge constants (Tantri's ContactString).
static const float kRest = 0.3f;
static const float kMin = -0.7f;
static const float kJawSmooth = 0.279f;        // ~2.5 kHz one-pole
static const float kJawAvg = 1.0f / 1440.0f;   // ~30 ms
static const float kDcPole = 1.0f - kTwoPi * 0.5f / kSampleRate;

// Transparent below 0.9, smooth saturation above (safety only).
static inline float Safe(float x) {
  float a = fabsf(x);
  if (a <= 0.9f) return x;
  float y = 0.9f + 0.1f * SoftClip((a - 0.9f) * 10.0f);
  return x < 0.0f ? -y : y;
}

static inline float AllpassDcDelay(float k) { return (1.0f + k) / (1.0f - k); }

bool TantriMode::Init(Arena* arena) {
  dayan_.modes = arena->Allocate<Mode2>(kDayanModes);
  bayan_.modes = arena->Allocate<Mode2>(kBayanModes);
  if (!dayan_.modes || !bayan_.modes) return false;
  dayan_.count = kDayanModes;
  bayan_.count = kBayanModes;
  Drum* drums[2] = { &dayan_, &bayan_ };
  for (Drum* d : drums) {
    d->contact = 0;
    d->contact_len = 1;
    d->strike_amp = 0.0f;
    d->impulse_norm = 1.0f;
    d->noise_env = 0.0f;
    d->noise_decay = expf(-1.0f / (0.012f * kSampleRate));
    for (int g = 0; g < 6; ++g) d->extra_damp[g] = 0.0f;
    d->noise_bp.Init();
    d->noise_bp.set(3000.0f / kSampleRate, 1.4f);
  }
  for (int i = 0; i < kStrings; ++i) {
    String& s = string_[i];
    s.line = arena->Allocate<float>(kLineSize);
    if (!s.line) return false;
    s.v = s.v_target = s.v_base = 200.0f;
    s.g = 0.99f;
    s.a = 0.1f;
    s.lp = s.hp_x = s.hp_y = 0.0f;
    s.ap[0] = s.ap[1] = s.ap[2] = 0.0f;
    s.jaw_s = s.jaw_avg = 0.0f;
    s.ex_pos = s.ex_len = 0;
    s.ex_amp = 0.0f;
    s.level = s.target = s.boost = 0.0f;
  }
  static const float kBodyHz[4] = { 128.0f, 255.0f, 448.0f, 1046.0f };
  static const float kBodyQ[4] = { 11.0f, 14.0f, 18.0f, 25.0f };
  for (int i = 0; i < 4; ++i) {
    body_[i].Init();
    body_[i].set(kBodyHz[i] / kSampleRate, kBodyQ[i]);
  }
  dayan_f0_ = 261.63f;
  bayan_f0_ = 98.0f;
  bayan_pm_ = 1.0f;
  ghe_env_ = 0.0f;
  ghe_dir_ = 0;
  jaw_depth_ = 0.0f;
  pluck_timer_ = 0.3f;
  pluck_step_ = 0;
  tanpura_level_ = 0.0f;
  last_sa_ = -1.0f;
  last_frozen_ = false;
  frozen_ = false;
  write_ = 0;
  rng_.Seed(0x7A47);
  dc_[0].Init();
  dc_[1].Init();
  return true;
}

// ---------------------------------------------------------------------------
// Tabla.

void TantriMode::StrikeDrum(Drum* d, const float* drive, float amp,
                            float contact_ms, const float* damp, float noise,
                            float noise_hz) {
  int len = static_cast<int>(contact_ms * 0.001f * kSampleRate);
  if (len < 2) len = 2;
  d->contact_len = len;
  d->contact = len;
  d->impulse_norm = 48.0f / static_cast<float>(len);
  d->strike_amp = amp;
  for (int i = 0; i < d->count; ++i) d->modes[i].drive = drive[i];
  for (int g = 0; g < 6; ++g) d->extra_damp[g] = damp[g];
  d->noise_bp.set(noise_hz / kSampleRate, 1.4f);
  d->noise_env = noise * amp;
}

void TantriMode::Play(int stroke) {
  const float v = 0.85f;      // velocity 0.8 in Tantri terms
  const float hard = 0.81f;   // contact-time scale for that velocity
  const float snap = 0.7f;    // noise scale (Tantri: snap 0.35 x 2)
  switch (stroke) {
    case NA:
    case DHA:
      StrikeDrum(&dayan_, kDayanDrive[0], v * 1.8f, 0.45f * hard, kNaDamp,
                 0.25f * snap, 3800.0f);
      break;
    case TIN:
    case DHIN:
      StrikeDrum(&dayan_, kDayanDrive[1], v, 0.7f * hard, kNoDamp,
                 0.12f * snap, 3200.0f);
      break;
    case TUN:
      StrikeDrum(&dayan_, kDayanDrive[2], v, 1.4f * hard, kNoDamp,
                 0.05f * snap, 2000.0f);
      break;
    case TE:
      StrikeDrum(&dayan_, kDayanDrive[3], v * 2.6f, 1.8f * hard, kTeDamp,
                 0.45f * snap, 1800.0f);
      break;
    case KE:
      StrikeDrum(&bayan_, kBayanDrive[1], v * 1.3f * 1.7f, 4.0f * hard,
                 kKeDamp, 0.6f * snap, 700.0f);
      break;
    default:
      break;
  }
  if (stroke == GE || stroke == DHA || stroke == DHIN) {
    StrikeDrum(&bayan_, kBayanDrive[0], v * 1.3f, 3.0f * hard, kNoDamp,
               0.06f * snap, 900.0f);
    ghe_env_ = 0.0001f;  // wrist slides forward: rise then settle
    ghe_dir_ = 1;
  }
}

void TantriMode::UpdateDrum(Drum* d, const float* ratios, const float* t60s,
                            const uint8_t* groups, float f0) {
  for (int i = 0; i < d->count; ++i) {
    Mode2& m = d->modes[i];
    float f = f0 * ratios[i];
    if (f > 0.24f * kSampleRate) {  // keep w < pi/2 (cos from sqrt)
      m.cr = m.ci = 0.0f;
      continue;
    }
    float t60 = t60s[i];
    float ed = d->extra_damp[groups[i]];
    if (ed > 0.0f) t60 = 1.0f / (1.0f / t60 + 1.0f / ed);
    float x = 6.9078f / (t60 * kSampleRate);
    float r = 1.0f - x + 0.5f * x * x;   // exp(-x), x < 0.01
    float s = FastSin(f / kSampleRate);
    float c = sqrtf(1.0f - s * s);
    m.cr = r * c;
    m.ci = r * s;
  }
}

inline float TantriMode::TickDrum(Drum* d, float input) {
  float force = 0.0f;
  if (d->contact > 0) {
    float ph = 1.0f - static_cast<float>(d->contact) /
        static_cast<float>(d->contact_len);
    force = d->strike_amp * (1.0f - FastCos(ph)) * d->impulse_norm;
    --d->contact;
  }
  float y = 0.0f;
  Mode2* m = d->modes;
  for (int i = 0; i < d->count; ++i) {
    float nre = m[i].cr * m[i].re - m[i].ci * m[i].im +
        force * m[i].drive + input;
    float nim = m[i].ci * m[i].re + m[i].cr * m[i].im;
    m[i].re = nre;
    m[i].im = nim;
    y += nim;
  }
  float noise = 0.0f;
  if (d->noise_env > 1e-5f) {
    noise = d->noise_bp.BpNormalized(rng_.Bipolar()) * d->noise_env;
    d->noise_env *= d->noise_decay;
  }
  return y * 0.06f + noise;
}

// ---------------------------------------------------------------------------
// Tanpura.

void TantriMode::TuneStrings(float sa, bool frozen) {
  // Pa (one octave down from the scale's fifth), Sa, Sa (beating), sa.
  string_hz_[0] = sa * 0.749154f;
  string_hz_[1] = sa * 0.999769f;
  string_hz_[2] = sa * 1.000520f;
  string_hz_[3] = sa * 0.5f;
  for (int i = 0; i < kStrings; ++i) {
    String& s = string_[i];
    const float f = string_hz_[i];
    const float bright = i == 3 ? 0.62f : 0.74f;
    s.a = Clamp(0.32f - 0.31f * bright, 0.004f, 0.6f);
    const float w = kTwoPi * f / kSampleRate;
    const float cw = cosf(w), sw = sinf(w);
    // Phase delay of loss filter + resting bridge allpasses at f.
    float ph = -atan2f(s.a * sw, 1.0f - s.a * cw);
    float ap = atan2f(-sw, cw - kRest) - atan2f(kRest * sw, 1.0f - kRest * cw);
    ph += 3.0f * ap;
    const float loop_delay = -ph / w;
    s.v_base = Clamp(kSampleRate / f - loop_delay, 4.0f,
                     static_cast<float>(kLineSize - 8));
    const float mag = (1.0f - s.a) /
        sqrtf(1.0f - 2.0f * s.a * cw + s.a * s.a);
    if (frozen) {
      s.g = 1.0f / mag;
    } else {
      const float t60 = (i == 3 ? 11.0f : 8.5f) * powf(f / 130.0f, -0.25f);
      s.g = powf(10.0f, -3.0f / (t60 * f)) / mag;
      if (s.g * mag > 0.99999f) s.g = 0.99999f / mag;
    }
    if (last_sa_ < 0.0f) s.v = s.v_target = s.v_base;
  }
}

void TantriMode::Pluck(int i, float amp) {
  String& s = string_[i];
  s.ex_len = static_cast<int>(0.0026f * kSampleRate *
                              (1.0f + 0.1f * rng_.Float()));
  s.ex_pos = 0;
  s.ex_amp = amp;
}

// ---------------------------------------------------------------------------

void TantriMode::Process(const Controls& c, const FloatFrame* in,
                         FloatFrame* out, size_t n) {
  frozen_ = c.freeze;
  const float dt = static_cast<float>(n) / kSampleRate;

  // Tuning.
  const float sa = Clamp(130.81f * SemitonesToRatio(c.pitch), 55.0f, 523.25f);
  if (frozen_ && !last_frozen_) {
    for (int i = 0; i < kStrings; ++i) {
      string_[i].target = sqrtf(string_[i].level);
    }
  }
  // While frozen the jawari keeps moving energy into harmonics the loop
  // filter then removes; a slow level servo on each string's loop gain
  // (like the bow of Tantri's drone mode) holds the drone where it was.
  for (int i = 0; i < kStrings; ++i) {
    String& s = string_[i];
    if (frozen_ && s.target > 1e-5f) {
      float rms = sqrtf(s.level) + 1e-7f;
      s.boost = Clamp(s.boost + 0.004f * (s.target / rms - 1.0f), 0.0f, 0.03f);
    } else {
      s.boost = 0.0f;
    }
  }
  if (fabsf(sa - last_sa_) > 0.01f || frozen_ != last_frozen_) {
    TuneStrings(sa, frozen_);
    last_sa_ = sa;
    last_frozen_ = frozen_;
  }
  dayan_f0_ = 2.0f * sa;
  bayan_f0_ = 0.75f * sa;

  // Ghe glide and palm pressure on the bayan.
  if (ghe_dir_ > 0) {
    ghe_env_ += dt / 0.12f;
    if (ghe_env_ >= 1.0f) { ghe_env_ = 1.0f; ghe_dir_ = -1; }
  } else if (ghe_dir_ < 0) {
    ghe_env_ -= dt / 0.9f;
    if (ghe_env_ <= 0.0f) { ghe_env_ = 0.0f; ghe_dir_ = 0; }
  }
  float press = Clamp(Clamp(c.size, 0.0f, 1.0f) + ghe_env_ * 0.55f, 0.0f, 1.5f);
  bayan_pm_ += 0.3f * (1.0f + 0.55f * press - bayan_pm_);

  if (c.trigger) {
    int stroke = static_cast<int>(Clamp(c.position, 0.0f, 0.999f) * NUM_STROKES);
    Play(stroke);
  }
  UpdateDrum(&dayan_, kDayanRatio, kDayanT60, kDayanGroup, dayan_f0_);
  UpdateDrum(&bayan_, kBayanRatio, kBayanT60, kBayanGroup,
             bayan_f0_ * bayan_pm_);

  // Jawari and the average-wrap pitch compensation.
  const float jaw = Clamp(c.texture, 0.0f, 1.0f);
  jaw_depth_ = 30.0f * powf(jaw, 1.3f);
  for (int i = 0; i < kStrings; ++i) {
    String& s = string_[i];
    float k_avg = kRest - (kRest - kMin) * s.jaw_avg;
    float comp = 3.0f * (AllpassDcDelay(kRest) - AllpassDcDelay(k_avg));
    s.v_target = Clamp(s.v_base + comp, 4.0f, static_cast<float>(kLineSize - 8));
  }

  // Auto-pluck cycle: Pa . Sa . Sa . sa . . (gap after the kharaj).
  const float density = Clamp(c.density, 0.0f, 1.0f);
  tanpura_level_ += 0.1f * (density - tanpura_level_);
  if (density > 0.02f && !frozen_) {
    pluck_timer_ -= dt;
    if (pluck_timer_ <= 0.0f) {
      int i = pluck_step_;
      float vel = 0.75f + 0.12f * rng_.Float();
      if (i == 3) vel *= 1.1f;
      Pluck(i, 0.2f * vel);
      float beat = 60.0f / (30.0f + 60.0f * density);
      pluck_timer_ += (i == 3 ? 2.0f * beat : beat) *
          (1.0f + 0.04f * rng_.Float());
      pluck_step_ = (pluck_step_ + 1) & 3;
    }
  }

  const float drum_in = frozen_ ? 0.0f : 0.004f;
  const float string_in = frozen_ ? 0.0f : 0.03f;
  const float tan_gain = 1.6f * tanpura_level_ + (frozen_ ? 0.6f : 0.25f);
  static const float kPan[kStrings] = { -0.48f, -0.16f, 0.16f, 0.48f };

  for (size_t t = 0; t < n; ++t) {
    const float x = 0.5f * (in[t].l + in[t].r);

    // Tabla: dayan on the right, bayan on the left (player's view).
    float dy = TickDrum(&dayan_, x * drum_in);
    float by = TickDrum(&bayan_, x * drum_in);
    float l = 0.62f * by + 0.38f * dy;
    float r = 0.62f * dy + 0.38f * by;

    // Tanpura strings.
    float force = 0.0f, sl = 0.0f, sr = 0.0f;
    for (int i = 0; i < kStrings; ++i) {
      String& s = string_[i];
      s.v += (s.v_target - s.v) * 0.002f;
      float pos = static_cast<float>(write_) - s.v;
      int32_t ip = static_cast<int32_t>(floorf(pos));
      float fr = pos - static_cast<float>(ip);
      const float* line = s.line;
      float y = Hermite(line[(ip - 1) & kLineMask], line[ip & kLineMask],
                        line[(ip + 1) & kLineMask], line[(ip + 2) & kLineMask],
                        fr);
      // Curved bridge: three lattice allpasses (rotations: passive)
      // whose coefficient follows how hard the string presses in.
      float press_in = -y;
      float want = press_in > 0.0f ? jaw_depth_ * press_in : 0.0f;
      if (want > 1.0f) want = 1.0f;
      s.jaw_s += (want - s.jaw_s) * kJawSmooth;
      s.jaw_avg += (s.jaw_s - s.jaw_avg) * kJawAvg;
      const float k = kRest - (kRest - kMin) * s.jaw_s;
      const float cc = sqrtf(1.0f - k * k);
      float b = y;
      for (int j = 0; j < 3; ++j) {
        float o = -k * b + cc * s.ap[j];
        s.ap[j] = cc * b + k * s.ap[j];
        b = o;
      }
      s.lp = (1.0f - s.a) * b + s.a * s.lp;
      float hp = s.lp - s.hp_x + kDcPole * s.hp_y;
      s.hp_x = s.lp;
      s.hp_y = hp;
      float v = s.g * (1.0f + s.boost) * hp + x * string_in;
      s.level += 0.0005f * (y * y - s.level);
      if (s.ex_pos < s.ex_len) {
        float ph = static_cast<float>(s.ex_pos) / static_cast<float>(s.ex_len);
        v += s.ex_amp * 0.5f * (1.0f - FastCos(ph));
        ++s.ex_pos;
      }
      // Safety only: never reached in normal playing.
      if (v > 4.0f) v = 4.0f;
      if (v < -4.0f) v = -4.0f;
      s.line[write_] = v;
      force += y;
      sl += y * (1.0f - kPan[i]);
      sr += y * (1.0f + kPan[i]);
    }
    write_ = (write_ + 1) & kLineMask;
    float body = 0.0f;
    static const float kBodyGain[4] = { 1.0f, 0.8f, 0.65f, 0.42f };
    for (int j = 0; j < 4; ++j) body += kBodyGain[j] * body_[j].BpNormalized(force);
    float tl = (0.4f * sl + 0.6f * body) * tan_gain;
    float tr = (0.4f * sr + 0.6f * body) * tan_gain;

    out[t].l = Safe(dc_[0].Process(l * 0.3f + tl));
    out[t].r = Safe(dc_[1].Process(r * 0.3f + tr));
  }
}

}  // namespace monsoon
