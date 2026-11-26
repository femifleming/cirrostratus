// CHLOE mode - see chloe.h. DSP ported from Chloaelis (Source/DSP.h).

#include "monsoon/dsp/modes/chloe.h"

namespace monsoon {
namespace chloe {

static inline float FastTanh(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float OnePoleCoef(float fc, float sr) {
  return 1.0f - expf(-kTwoPi * fc / sr);
}

// ---------------------------------------------------------------------------
__attribute__((cold)) void Biquad::Lowpass(float fc, float q) {
  fc = Clamp(fc, 10.0f, kSampleRate * 0.45f);
  const float w = kTwoPi * fc / kSampleRate, c = cosf(w);
  const float al = sinf(w) / (2.0f * q), n = 1.0f / (1.0f + al);
  b0 = (1.0f - c) * 0.5f * n; b1 = (1.0f - c) * n; b2 = b0;
  a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
}

__attribute__((cold)) void Biquad::Highpass(float fc, float q) {
  fc = Clamp(fc, 5.0f, kSampleRate * 0.45f);
  const float w = kTwoPi * fc / kSampleRate, c = cosf(w);
  const float al = sinf(w) / (2.0f * q), n = 1.0f / (1.0f + al);
  b0 = (1.0f + c) * 0.5f * n; b1 = -(1.0f + c) * n; b2 = b0;
  a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
}

__attribute__((cold)) void Biquad::Peak(float fc, float q, float db) {
  fc = Clamp(fc, 10.0f, kSampleRate * 0.45f);
  const float A = powf(10.0f, db / 40.0f), w = kTwoPi * fc / kSampleRate;
  const float c = cosf(w), al = sinf(w) / (2.0f * q);
  const float n = 1.0f / (1.0f + al / A);
  b0 = (1.0f + al * A) * n; b1 = -2.0f * c * n; b2 = (1.0f - al * A) * n;
  a1 = b1; a2 = (1.0f - al / A) * n;
}

// ---------------------------------------------------------------------------
float MultiModeFilter::Process(float x, float g, float k, float morph) {
  x = FastTanh(x * 0.6f) * (1.0f / 0.6f);
  const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
  const float v3 = x - ic2;
  const float v1 = a1 * ic1 + a2 * v3;
  const float v2 = ic2 + a2 * ic1 + a3 * v3;
  ic1 = 2.0f * v1 - ic1;
  ic2 = 2.0f * v2 - ic2;
  ic1 = 3.0f * FastTanh(ic1 * (1.0f / 3.0f));
  const float lp = v2, bp = v1 * k, hp = x - k * v1 - v2, notch = lp + hp;
  const float outs[5] = { lp, bp, hp, notch, notch };
  const float m = Clamp(morph, 0.0f, 3.0f);
  const int s = static_cast<int>(m);
  const float t = m - static_cast<float>(s);
  return outs[s] + (outs[s + 1] - outs[s]) * t;
}

// ---------------------------------------------------------------------------
// The 12AX7 stage transfer: grid conduction (g/(1+0.85g) above 0 V) then
// the plate curve softplus(4(vg+1))/4 ^ 1.5, as one table over the grid
// voltage, normalised so that the small-signal slope at vg = 0 is 1 and
// the output at 0 V is 0.
static const int kPlateTableSize = 1024;
static const float kPlateMin = -3.0f, kPlateMax = 13.0f;
static float plate_table[kPlateTableSize + 1];
static bool plate_table_ready = false;

__attribute__((cold)) static float PlateExact(float vg) {
  float x = 4.0f * (vg + 1.0f);
  float s = (x > 12.0f ? x : log1pf(expf(x))) * 0.25f;
  return s * sqrtf(s);
}

__attribute__((cold)) static void BuildPlateTable() {
  if (plate_table_ready) return;
  const float ip0 = PlateExact(0.0f);
  const float inv_slope = 0.002f / (PlateExact(0.001f) - PlateExact(-0.001f));
  for (int i = 0; i <= kPlateTableSize; ++i) {
    float vg = kPlateMin + (kPlateMax - kPlateMin) * i / kPlateTableSize;
    float g = vg > 0.0f ? vg / (1.0f + 0.85f * vg) : vg;
    plate_table[i] = (PlateExact(g) - ip0) * inv_slope;
  }
  plate_table_ready = true;
}

static inline float Plate(float vg) {
  float p = (vg - kPlateMin) * (kPlateTableSize / (kPlateMax - kPlateMin));
  if (p <= 0.0f) return plate_table[0];
  if (p >= kPlateTableSize) return plate_table[kPlateTableSize];
  int i = static_cast<int>(p);
  float f = p - static_cast<float>(i);
  return plate_table[i] + (plate_table[i + 1] - plate_table[i]) * f;
}

__attribute__((cold)) void TriodeStage::Init(float osr) {
  att = OnePoleCoef(1.0f / (kTwoPi * 0.002f), osr);
  rel = OnePoleCoef(1.0f / (kTwoPi * 0.120f), osr);
  bias = 0.0f;
}

inline float TriodeStage::Process(float vg) {
  vg -= bias;
  // Grid current charges the coupling cap (blocking distortion / sag).
  const float charge = (vg > 0.0f ? vg : 0.0f) * 0.35f;
  bias += (charge - bias) * (charge > bias ? att : rel);
  return Plate(vg);  // table includes the grid-conduction clamp
}

// 2x oversampling with a 4-coefficient polyphase IIR half-band
// (Niemitalo, ~70 dB rejection).
static const float kHb[4] = { 0.07986642623635751f, 0.5453536510711322f,
                              0.28382934487410993f, 0.8344118914807379f };

static inline float Allpass1(float x, float a, float* s) {
  float y = *s + a * x;
  *s = x - a * y;
  return y;
}

__attribute__((cold)) void Tube::Init() {
  BuildPlateTable();
  const float osr = 2.0f * kSampleRate;
  v1.Init(osr);
  v2.Init(osr);
  inter.r = 1.0f - kTwoPi * 12.0f / osr;
  inter.x1 = inter.y1 = 0;
  dc.r = 1.0f - kTwoPi * 7.0f / osr;
  dc.x1 = dc.y1 = 0;
  post_a = OnePoleCoef(15000.0f, osr);
  miller = out1 = 0.0f;
  for (int i = 0; i < 4; ++i) up_state[i] = down_state[i] = 0.0f;
  onmix = 0.0f;
}

float Tube::Process(float x, float drive, float gain, float makeup,
                    float miller_a, float c2, bool on) {
  // Upsample: the two polyphase branches give the two sub-samples.
  float u0 = Allpass1(Allpass1(x, kHb[0], &up_state[0]), kHb[2], &up_state[1]);
  float u1 = Allpass1(Allpass1(x, kHb[1], &up_state[2]), kHb[3], &up_state[3]);
  float y[2];
  float xs[2] = { u1, u0 };
  for (int k = 0; k < 2; ++k) {
    float s = xs[k] * gain * 0.5f;
    miller += miller_a * (s - miller);
    float v = v1.Process(miller);
    v = inter.Process(v);
    v = v2.Process(v * c2);
    out1 += post_a * (v - out1);
    y[k] = dc.Process(out1) * makeup;
  }
  // Downsample.
  float d0 = Allpass1(Allpass1(y[1], kHb[0], &down_state[0]), kHb[2],
                      &down_state[1]);
  float d1 = Allpass1(Allpass1(y[0], kHb[1], &down_state[2]), kHb[3],
                      &down_state[3]);
  float wet = 0.5f * (d0 + d1);
  // "on" crossfades the triodes in/out (TEXTURE fully down = clean).
  const float target = on ? 1.0f : 0.0f;
  onmix += Clamp(target - onmix, -0.004f, 0.004f);
  (void)drive;
  return x + (wet - x) * onmix;
}

// ---------------------------------------------------------------------------
__attribute__((cold)) bool Pt2399Line::Init(Arena* arena, uint32_t seed) {
  mem = arena->Allocate<uint8_t>(kBits / 8);
  if (!mem) return false;
  // Idle pattern 0101... = silence.
  memset(mem, 0xAA, kBits / 8);
  pos = 0;
  bit_phase = prev_in = held = 0.0f;
  enc_est = enc_step = dec_est = dec_step = 0.0f;
  enc_hist = dec_hist = 0;
  clock_a = OnePoleCoef(12.0f, kSampleRate);
  dc.Init(10.0f);
  rng.Seed(seed);
  bit_rate = target_rate = kBits / 0.33f;
  UpdateCoeffs();
  return true;
}

__attribute__((cold)) void Pt2399Line::UpdateCoeffs() {
  const float r = rate();
  coeff_rate = r;
  leak = expf(-kTwoPi * 25.0f / r);
  beta = expf(-1.0f / (0.0035f * r));
  const float isq = 1.0f / sqrtf(r);
  d_min = 2.6f * isq;
  d_max = 145.0f * isq;
  const float bw = Clamp(r * 0.048f, 2400.0f, 11500.0f);
  in_lp.Lowpass(bw, 0.62f);
  out_lp1.Lowpass(bw, 0.54f);
  out_lp2.Lowpass(bw * 1.25f, 1.3f);
}

float Pt2399Line::Process(float x, bool freeze) {
  bit_rate += (target_rate - bit_rate) * clock_a;
  float in = freeze ? 0.0f : x;
  in = in_lp.Process(in);
  in = 1.1f * FastTanh(in * 0.9f);
  // Past kMaxRate: the clock stays there and the line gets shorter, so the
  // delay (bits / clock) is still kBits / bit_rate.
  float r = bit_rate;
  int len = kBits;
  if (r > kMaxRate) {
    len = static_cast<int>(static_cast<float>(kBits) * kMaxRate / r);
    r = kMaxRate;
  }
  bit_phase += r * (1.0f / kSampleRate);
  const int n = static_cast<int>(bit_phase);
  bit_phase -= static_cast<float>(n);
  if (n > 0) {
    // Local copies (byte stores could alias the members), the RAM byte
    // under the head cached in a register, branch-free step targets:
    // run-of-3 coincidence (history 000 or 111) selects d_max.
    float e_est = enc_est, e_step = enc_step, d_est = dec_est, d_step = dec_step;
    unsigned e_hist = enc_hist, d_hist = dec_hist;
    int p = pos;
    uint8_t* const m = mem;
    const float lk = leak, b = beta;
    const float t_max = (1.0f - beta) * d_max, t_min = (1.0f - beta) * d_min;
    const float target[8] = { t_max, t_min, t_min, t_min,
                              t_min, t_min, t_min, t_max };
    const float dx = (in - prev_in) / static_cast<float>(n);
    float xin = prev_in;
    float acc = 0.0f;
    int byte_index = p >> 3;
    unsigned cur = m[byte_index];
    for (int k = 0; k < n; ++k) {
      xin += dx;
      const unsigned bit = xin > e_est ? 1u : 0u;
      e_hist = ((e_hist << 1) | bit) & 7u;
      e_step = b * e_step + target[e_hist];
      e_est = e_est * lk + (bit ? e_step : -e_step);

      const unsigned shift = static_cast<unsigned>(p) & 7u;
      const unsigned out_bit = (cur >> shift) & 1u;
      if (!freeze) cur = (cur & ~(1u << shift)) | (bit << shift);

      d_hist = ((d_hist << 1) | out_bit) & 7u;
      d_step = b * d_step + target[d_hist];
      d_est = d_est * lk + (out_bit ? d_step : -d_step);
      acc += d_est;

      if (++p >= len) p = 0;
      if ((p & 7) == 0) {
        m[byte_index] = static_cast<uint8_t>(cur);
        byte_index = p >> 3;
        cur = m[byte_index];
      }
    }
    m[byte_index] = static_cast<uint8_t>(cur);
    enc_est = e_est; enc_step = e_step; dec_est = d_est; dec_step = d_step;
    enc_hist = e_hist; dec_hist = d_hist;
    pos = p;
    held = acc / static_cast<float>(n);
  }
  prev_in = in;
  float y = out_lp2.Process(out_lp1.Process(held));
  y = 1.3f * FastTanh(dc.Process(y) * (1.0f / 1.3f));
  y += (rng.Float() - 0.5f) * 0.00012f;
  return y;
}

// ---------------------------------------------------------------------------
static const float kBbdRef = 0.22f;

__attribute__((cold)) bool BbdLine::Init(Arena* arena, uint32_t seed) {
  bucket = arena->Allocate<float>(kStages);
  if (!bucket) return false;
  idx = 0;
  tick_phase = prev_aa = held = cti = 0.0f;
  parity = 1.0f;
  comp_env = exp_env = 0.05f;
  clk_a = OnePoleCoef(9.0f, kSampleRate);
  aa1.Lowpass(6800.0f, 0.54f); aa2.Lowpass(6800.0f, 1.31f);
  rc1.Lowpass(6200.0f, 0.54f); rc2.Lowpass(6200.0f, 1.31f);
  c_att = OnePoleCoef(1.0f / (kTwoPi * 0.004f), kSampleRate);
  c_rel = OnePoleCoef(1.0f / (kTwoPi * 0.045f), kSampleRate);
  e_att = OnePoleCoef(1.0f / (kTwoPi * 0.006f), kSampleRate);
  e_rel = OnePoleCoef(1.0f / (kTwoPi * 0.060f), kSampleRate);
  dc.Init(9.0f);
  rng.Seed(seed);
  clk = target_clk = kStages / 0.33f;
  return true;
}

static inline float BucketSat(float v) {
  return 0.85f * (FastTanh(v * 1.15f + 0.06f) - 0.0599f);
}

float BbdLine::Process(float x, bool freeze) {
  clk += (target_clk - clk) * clk_a;
  const float ax = fabsf(x);
  comp_env += (ax - comp_env) * (ax > comp_env ? c_att : c_rel);
  float env = comp_env > 0.0005f ? comp_env : 0.0005f;
  float g = 1.0f / sqrtf(env / kBbdRef);
  if (g > 10.0f) g = 10.0f;
  const float aa = aa2.Process(aa1.Process(x * g));
  float ck = clk;
  int stages = kStages;
  if (ck > kMaxClk) {
    stages = static_cast<int>(static_cast<float>(kStages) * kMaxClk / ck);
    ck = kMaxClk;
  }
  const float inc = ck * (1.0f / kSampleRate);
  const float inv_inc = 1.0f / inc;
  tick_phase += inc;
  while (tick_phase >= 1.0f) {
    tick_phase -= 1.0f;
    const float frac = Clamp(1.0f - tick_phase * inv_inc, 0.0f, 1.0f);
    const float v = prev_aa + (aa - prev_aa) * frac;
    const float out = bucket[idx];
    bucket[idx] = freeze ? out * 0.99985f
                         : BucketSat(v) + (rng.Float() - 0.5f) * 0.0009f;
    if (++idx >= stages) idx = 0;
    cti += 0.86f * (out - cti);
    held = cti;
    parity = -parity;
  }
  prev_aa = aa;
  float y = rc2.Process(rc1.Process(held + parity * 0.0012f));
  y += (rng.Float() - 0.5f) * 0.0004f;
  const float ay = fabsf(y);
  exp_env += (ay - exp_env) * (ay > exp_env ? e_att : e_rel);
  float e = exp_env > 0.0002f ? exp_env : 0.0002f;
  float eg = e / kBbdRef;
  if (eg > 6.0f) eg = 6.0f;
  y = dc.Process(y * eg);
  return 1.25f * FastTanh(y * (1.0f / 1.25f));
}

// ---------------------------------------------------------------------------
static const float kTapeScale = 16384.0f;
static const float kTapeWear = 0.2f;  // a tape that has done a few gigs

__attribute__((cold)) bool TapeLine::Init(Arena* arena, uint32_t seed) {
  size = kMaxFrames;
  buf = arena->Allocate<int16_t>(size);
  if (!buf) return false;
  rng.Seed(seed);
  w = 0;
  d_a = d_s = d_target = 0.33f * kSampleRate;
  slew_a = OnePoleCoef(1.6f, kSampleRate);
  frozen = false;
  primed = false;
  loop_start = loop_len = loop_pos = 0.0f;
  wow_t = wow1 = wow2 = 0.0f;
  wow_a = OnePoleCoef(0.55f, kSampleRate);
  wow_timer = 0.0f;
  flut_ph = flut_ph2 = 0.0f;
  flut_inc = (6.1f + rng.Float() * 0.8f) / kSampleRate;
  flut_inc2 = (11.3f + rng.Float() * 1.5f) / kSampleRate;
  pre = gap1 = gap2 = deemph = hiss_lp = hiss_hp = 0.0f;
  pre_a = OnePoleCoef(2400.0f, kSampleRate);
  const float fc = 15000.0f * (1.0f - 0.8f * sqrtf(kTapeWear));
  const float wc = kTwoPi * fc / kSampleRate;
  gap_a = wc / (1.0f + wc);
  hiss_a = OnePoleCoef(11000.0f, kSampleRate);
  hiss_ha = OnePoleCoef(900.0f, kSampleRate);
  bump.Peak(95.0f, 0.9f, 2.5f);
  dc.Init(8.0f);
  return true;
}

void TapeLine::SetTime(float sec) {
  const float wow_depth = 0.0021f * kSampleRate;
  d_target = Clamp(sec * kSampleRate, 64.0f,
                   static_cast<float>(size) - wow_depth * 4.0f - 32.0f);
  if (!primed) {
    d_a = d_s = d_target;
    primed = true;
  }
}

inline float TapeLine::ReadAt(float p) const {
  const float n = static_cast<float>(size);
  while (p < 0.0f) p += n;
  while (p >= n) p -= n;
  const int i = static_cast<int>(p);
  const float f = p - static_cast<float>(i);
  int im1 = i - 1 < 0 ? size - 1 : i - 1;
  int i1 = i + 1 >= size ? i + 1 - size : i + 1;
  int i2 = i + 2 >= size ? i + 2 - size : i + 2;
  const float s = 1.0f / kTapeScale;
  return Hermite(buf[im1] * s, buf[i] * s, buf[i1] * s, buf[i2] * s, f);
}

float TapeLine::Process(float in, bool freeze) {
  const float wear = sqrtf(kTapeWear);
  const float wow_depth = 0.0021f * kSampleRate;
  const float flut_depth = 0.000028f * kSampleRate;
  wow_timer -= 1.0f;
  if (wow_timer <= 0.0f) {
    wow_t = rng.Bipolar();
    wow_timer = kSampleRate * (0.5f + 1.8f * rng.Float());
  }
  wow1 += (wow_t - wow1) * wow_a;
  wow2 += (wow1 - wow2) * wow_a;
  flut_ph += flut_inc; if (flut_ph >= 1.0f) flut_ph -= 1.0f;
  flut_ph2 += flut_inc2; if (flut_ph2 >= 1.0f) flut_ph2 -= 1.0f;
  const float wob = wow_depth * (1.0f + 1.6f * wear) * wow2 +
      flut_depth * (1.0f + 2.0f * wear) *
      (FastSin(flut_ph) + 0.35f * FastSin(flut_ph2));

  if (freeze && !frozen) {
    frozen = true;
    loop_len = d_a > 256.0f ? d_a : 256.0f;
    loop_start = static_cast<float>(w) - 1.0f - d_a;
    loop_pos = 0.0f;
  } else if (!freeze && frozen) {
    frozen = false;
  }

  float out;
  if (!frozen) {
    pre += (in - pre) * pre_a;
    const float e = in + 0.6f * (in - pre);
    float m = (FastTanh(e * 1.15f + 0.04f) - 0.04f) * (1.0f - 0.18f * wear);
    float q = Clamp(m, -1.99f, 1.99f) * kTapeScale;
    buf[w] = static_cast<int16_t>(q);
    if (++w >= size) w = 0;
    d_s += (d_target - d_s) * slew_a;
    d_a += (d_s - d_a) * slew_a;
    float d = d_a + wob;
    if (d < 8.0f) d = 8.0f;
    out = ReadAt(static_cast<float>(w) - 1.0f - d);
  } else {
    loop_pos += 1.0f;
    if (loop_pos >= loop_len) loop_pos -= loop_len;
    const float p = loop_start + loop_pos - wob * 0.5f;
    out = ReadAt(p);
    const float xlen = loop_len * 0.25f < 600.0f ? loop_len * 0.25f : 600.0f;
    if (loop_pos > loop_len - xlen) {
      const float t = (loop_pos - (loop_len - xlen)) / xlen;
      out = out * (1.0f - t) + ReadAt(p - loop_len) * t;
    }
  }

  gap1 += (out - gap1) * gap_a;
  gap2 += (gap1 - gap2) * gap_a;
  float y = bump.Process(gap2);
  deemph += (y - deemph) * pre_a;
  y -= 0.375f * (y - deemph);
  const float nz = rng.Bipolar();
  hiss_lp += (nz - hiss_lp) * hiss_a;
  hiss_hp += (hiss_lp - hiss_hp) * hiss_ha;
  y += (hiss_lp - hiss_hp) * (0.0018f + 0.008f * wear) * 0.5f;
  return dc.Process(y);
}

// ---------------------------------------------------------------------------
// Mutually prime lengths (2.4 - 17 ms), a little different on each side.
static const int kDiffLen[2][Diffuser::kStages] = {
  { 113, 337, 509, 797 }, { 127, 349, 547, 829 }
};
static const float kDiffG = 0.6f;

__attribute__((cold)) bool Diffuser::Init(Arena* arena, int side) {
  int total = 0;
  for (int k = 0; k < kStages; ++k) {
    len[k] = kDiffLen[side][k];
    pos[k] = 0;
    total += len[k];
  }
  buf = arena->Allocate<float>(total);
  return buf != NULL;
}

__attribute__((cold)) void Diffuser::Clear() {
  int total = 0;
  for (int k = 0; k < kStages; ++k) total += len[k];
  memset(buf, 0, total * sizeof(float));
}

inline float Diffuser::Process(float x) {
  float* b = buf;
  for (int k = 0; k < kStages; ++k) {
    const float d = b[pos[k]];
    const float w = x + kDiffG * d;
    b[pos[k]] = w;
    x = d - kDiffG * w;
    if (++pos[k] >= len[k]) pos[k] = 0;
    b += len[k];
  }
  return x;
}

// ---------------------------------------------------------------------------
__attribute__((cold)) bool KsLine::Init(Arena* arena) {
  buf = arena->Allocate<float>(kSize);
  if (!buf) return false;
  w = 0;
  lp = 0.0f;
  d_a = d_target = 100.0f;
  slew_a = OnePoleCoef(11.0f, kSampleRate);
  dc.Init(1.5f);
  dc_lead = 1.5f / (kTwoPi * kSampleRate);
  msq = 0.0f;
  msq_a = 0.001f;
  pk = 0.0f;
  return true;
}

void KsLine::SetPeriod(float sec) {
  d_target = Clamp(sec * kSampleRate, 5.0f, static_cast<float>(kSize) - 8.0f);
  float a = 0.25f / d_target;
  float b = 1.0f / (0.005f * kSampleRate);
  msq_a = a < b ? a : b;
}

inline float KsLine::ReadAt(float d) const {
  float p = static_cast<float>(w) - d;
  if (p < 0.0f) p += static_cast<float>(kSize);
  const int i = static_cast<int>(p);
  const float f = p - static_cast<float>(i);
  int im1 = i - 1 < 0 ? i - 1 + kSize : i - 1;
  int i1 = i + 1 >= kSize ? i + 1 - kSize : i + 1;
  int i2 = i + 2 >= kSize ? i + 2 - kSize : i + 2;
  return Hermite(buf[im1], buf[i], buf[i1], buf[i2], f);
}

// Level governor from Chloaelis 1.2: regeneration eases down as the
// string's RMS reaches the ceiling, so it sings cleanly past 100 %.
float KsLine::Feedback(float f, float regen) {
  const float ceiling = 0.2f, peak_ceiling = 0.5f;
  msq += (f * f - msq) * msq_a;
  const float a = fabsf(f);
  pk = a > pk ? a : pk * (1.0f - 2.0f * msq_a);
  float lim = msq > ceiling * ceiling ? ceiling / sqrtf(msq) : 1.0f;
  float lim2 = pk > peak_ceiling ? peak_ceiling / pk : 1.0f;
  if (lim2 < lim) lim = lim2;
  return 4.0f * FastTanh(0.25f * f * regen * lim);
}

float KsLine::Process(float in, bool freeze, float damp) {
  if (freeze) {
    const float y = ReadAt(d_a > 3.0f ? d_a : 3.0f);
    buf[w] = y;
    if (++w >= kSize) w = 0;
    return y;
  }
  d_a += (d_target - d_a) * slew_a;
  float line = d_a - 1.0f - 0.5f * damp + dc_lead * d_a * d_a;
  if (line < 3.0f) line = 3.0f;
  float y = ReadAt(line);
  buf[w] = in;
  if (++w >= kSize) w = 0;
  const float o = y + (lp - y) * (0.5f * damp);
  lp = y;
  return dc.Process(o);
}

}  // namespace chloe

// ---------------------------------------------------------------------------
using namespace chloe;

__attribute__((cold)) bool ChloeMode::Init(Arena* arena) {
  engine_ = init_option_ & 3;
  delay_first_ = init_option_ >= 4;
  for (int i = 0; i < 2; ++i) {
    Side& s = side_[i];
    s.filter.ic1 = s.filter.ic2 = 0.0f;
    s.tube.Init();
    s.pt = NULL; s.bbd = NULL; s.tape = NULL; s.ks = NULL;
    s.fb = 0.0f;
    s.xfb = s.xfb_x1 = s.xfb_y1 = 0.0f;
    s.fb_hp.Reset();
    s.fb_hp.Highpass(120.0f, 0.6f);
    uint32_t seed = 0x2399 + 77 * i;
    switch (engine_) {
      case ENGINE_PT2399:
        s.pt = arena->Allocate<Pt2399Line>(1);
        if (!s.pt || !s.pt->Init(arena, seed)) return false;
        break;
      case ENGINE_BBD:
        s.bbd = arena->Allocate<BbdLine>(1);
        if (!s.bbd || !s.bbd->Init(arena, seed)) return false;
        break;
      case ENGINE_TAPE:
        s.tape = arena->Allocate<TapeLine>(1);
        if (!s.tape || !s.tape->Init(arena, seed)) return false;
        break;
      default:
        s.ks = arena->Allocate<KsLine>(1);
        if (!s.ks || !s.ks->Init(arena)) return false;
        break;
    }
    s.diff = arena->Allocate<Diffuser>(1);
    if (!s.diff || !s.diff->Init(arena, i)) return false;
  }
  diffuse_ = 0.0f;
  cross_ = 0.0f;
  diff_live_ = false;
  lx_ = 0.1f; ly_ = 0.0f; lz_ = 20.0f;
  rng_.Seed(0x10e7);
  g_[0] = g_[1] = 0.1f;
  cut_semis_[0] = cut_semis_[1] = 0.0f;
  drive_ = 0.0f;
  morph_ = 0.0f;
  random_freeze_timer_ = 0.0f;
  pluck_timer_ = 0.0f;
  was_trigger_ = false;
  return true;
}

float ChloeMode::RunDelay(Side* s, float x, float time, float regen,
                          bool freeze, float ks_damp, float diffuse) {
  float y;
  if (s->ks) {
    s->ks->SetPeriod(time);
    y = s->ks->Process(x + s->fb, freeze, ks_damp);
    // No high-pass in the string's loop (the line's own DC blocker is
    // enough), or low notes would die.
    s->fb = freeze ? 0.0f : s->ks->Feedback(y, regen);
    // RVB: the string's output through the diffuser (not its loop: that
    // would detune it).
    if (diffuse > 0.0f) y += diffuse * (s->diff->Process(y) - y);
    return y;
  }
  const float in = x + s->fb;
  if (s->pt) {
    s->pt->SetTime(time);
    y = s->pt->Process(in, freeze);
  } else if (s->bbd) {
    s->bbd->SetTime(time);
    y = s->bbd->Process(in, freeze);
  } else {
    s->tape->SetTime(time);
    y = s->tape->Process(in, freeze);
  }
  float f = s->fb_hp.Process(y);
  // RVB: the repeats go round through the diffuser (a unity-gain allpass
  // chain crossfaded in, so the loop gain never rises).
  if (diffuse > 0.0f) f += diffuse * (s->diff->Process(f) - f);
  s->fb = freeze ? 0.0f : 1.05f * FastTanh(f * regen);
  return y;
}

void ChloeMode::Process(const Controls& c, const FloatFrame* in,
                        FloatFrame* out, size_t n) {
  // --- Control rate -------------------------------------------------------
  // Lorenz attractor: speed fixed slow, x drives left cutoff, y right.
  // BAL sets the speed: x0.25 (left) .. x1 (noon) .. x8 (right).
  const float bal = KnobBipolar(c.bal);
  {
    const float rho = 28.0f, beta = 8.0f / 3.0f, sigma = 10.0f;
    const float speed = bal > 0.0f ? 1.0f + 7.0f * bal : 1.0f + 0.75f * bal;
    const float dt = 0.35f * speed * static_cast<float>(n) / kSampleRate;
    const int steps = 2;
    const float h = dt / steps;
    for (int k = 0; k < steps; ++k) {
      float dx = sigma * (ly_ - lx_);
      float dy = lx_ * (rho - lz_) - ly_;
      float dz = lx_ * ly_ - beta * lz_;
      lx_ += h * dx; ly_ += h * dy; lz_ += h * dz;
    }
    lx_ += (rng_.Float() - 0.5f) * 0.002f;
    if (!(fabsf(lx_) < 1e4f && fabsf(ly_) < 1e4f && fabsf(lz_) < 1e4f)) {
      lx_ = 0.1f; ly_ = 0.0f; lz_ = 20.0f;
    }
  }
  const float scale = sqrtf(8.0f / 3.0f * 27.0f);
  const float lor_x = FastTanh(lx_ / (1.4f * scale));
  const float lor_y = FastTanh(ly_ / (1.9f * scale));
  const float lor_z = Clamp((lz_ - 27.0f) / 28.0f, -1.0f, 1.0f);
  // BAL: depth of the attractor's pull on the cutoffs and on the filter
  // mode (its speed was applied to the step above).
  float lor_depth = 9.0f, morph_depth = 0.5f;
  if (bal < 0.0f) {
    lor_depth *= 1.0f + bal;
    morph_depth *= 1.0f + bal;
  } else if (bal > 0.0f) {
    lor_depth += 27.0f * bal;    // up to +/-36 semitones
    morph_depth += 1.0f * bal;   // LP <-> BP <-> HP
  }

  // POSITION: cutoff 30 Hz .. 16 kHz; TUNE transposes it (delay engines);
  // the Lorenz attractor pushes left and right up to +/- 9 semitones apart.
  const bool string = engine_ == ENGINE_STRING;
  const float base = 30.0f * exp2f(c.position * 9.06f);
  const float tune = string ? 0.0f : c.pitch;
  // Resonance rises toward the top of TEXTURE's travel together with drive;
  // the filter mode wanders slowly between LP and BP with the attractor.
  const float res = 0.35f + 0.5f * c.texture;
  const float k = 2.0f - 1.96f * res;
  morph_ = 0.5f + morph_depth * lor_z;  // 0..1: LP -> BP (BAL: 0..2)
  if (morph_ < 0.0f) morph_ = 0.0f;
  for (int i = 0; i < 2; ++i) {
    const float lor = i == 0 ? lor_x : lor_y;
    float fc = base * SemitonesToRatio(tune + lor_depth * lor);
    fc = Clamp(fc, 12.0f, kSampleRate * 0.46f);
    g_[i] = tanf(kPi * fc / kSampleRate);
  }

  // TEXTURE: tube drive; fully down = tube out of circuit.
  drive_ = c.texture;
  const bool tube_on = c.texture > 0.02f;
  const float gain = exp2f(drive_ * 7.0f);
  const float miller_a = OnePoleCoef(18000.0f - 10500.0f * drive_,
                                     2.0f * kSampleRate);
  const float c2 = 0.35f + 0.9f * drive_;
  const float makeup = 1.0f / (0.5f * c2 * powf(gain, 0.36f));

  // SIZE: delay time 20 ms .. 1.6 s (tape stops at 0.6 s). In the string
  // engine TUNE/V-OCT is the note (C3 at noon) and SIZE the interval of the
  // right string over the left (unison, fifth, octave, ... two octaves).
  float time[2];
  if (string) {
    static const float kIntervals[6] = { 0.0f, 7.0f, 12.0f, 19.0f, 24.0f, 5.0f };
    int iv = static_cast<int>(c.size * 5.99f);
    float hz = 130.81f * SemitonesToRatio(c.pitch);
    time[0] = 1.0f / hz;
    time[1] = 1.0f / (hz * SemitonesToRatio(kIntervals[iv]));
  } else {
    float t = 0.02f * exp2f(c.size * 6.32f);
    time[0] = t;
    time[1] = t * (1.0f + 0.02f * lor_x);  // the sides drift a hair apart
  }
  // DENSITY: regen; past ~0.87 it runs away (sings), like the plug-in.
  const float regen = c.density * 1.15f;
  const float ks_damp = 0.25f;

  // FREEZE holds the delay; TRIG = random freeze (holds for 0.2-1.5 s) or,
  // in the string engine, plucks both strings.
  if (c.trigger && !was_trigger_) {
    if (string) {
      pluck_timer_ = time[0] * kSampleRate;  // one period of noise
    } else {
      random_freeze_timer_ = (0.2f + 1.3f * rng_.Float()) * kSampleRate;
    }
  }
  was_trigger_ = c.trigger;
  if (!string && side_[0].pt) {
    side_[0].pt->MaybeUpdateCoeffs();
    side_[1].pt->MaybeUpdateCoeffs();
  }

  // RVB: diffusion amount (smoothed; the lines are cleared when it comes
  // back from zero, so no old cloud replays).
  const float rvb = Clamp(c.rvb, 0.0f, 1.0f);
  if (rvb > 0.0f && !diff_live_) {
    side_[0].diff->Clear();
    side_[1].diff->Clear();
    diff_live_ = true;
  }
  diffuse_ += 0.1f * (rvb - diffuse_);
  if (rvb == 0.0f && diffuse_ < 1e-3f) {
    diffuse_ = 0.0f;
    diff_live_ = false;
  }
  const float diffuse = diffuse_;
  // FBK: crossed master feedback, ramped across the block. Divided by the
  // tube's small-signal gain (gain^0.64 with its makeup), so the knob means
  // the same at any drive: it sings only near the top with regen.
  float cross_target = 0.95f * Clamp(c.fbk, 0.0f, 1.0f);
  if (tube_on && cross_target > 0.0f) cross_target /= gain * makeup * 0.5f * c2;
  const bool cross = cross_target > 0.0f || cross_ > 0.0f;
  const float cross_inc = (cross_target - cross_) / static_cast<float>(n);
  const float xfb_r = 1.0f - kTwoPi * 20.0f / kSampleRate;

  // --- Audio rate ---------------------------------------------------------
  const float echo = 0.8f;
  for (size_t s = 0; s < n; ++s) {
    bool freeze = c.freeze || random_freeze_timer_ > 0.0f;
    if (random_freeze_timer_ > 0.0f) random_freeze_timer_ -= 1.0f;
    float io[2] = { in[s].l, in[s].r };
    if (cross) {
      cross_ += cross_inc;
      io[0] += cross_ * side_[1].xfb;
      io[1] += cross_ * side_[0].xfb;
    }
    if (pluck_timer_ > 0.0f) {
      pluck_timer_ -= 1.0f;
      float burst = rng_.Bipolar() * 0.9f;
      io[0] += burst;
      io[1] += burst;
    }
    for (int i = 0; i < 2; ++i) {
      Side& sd = side_[i];
      float x = io[i];
      float y;
      if (!delay_first_) {
        x = sd.filter.Process(x, g_[i], k, morph_);
        x = sd.tube.Process(x, drive_, gain, makeup, miller_a, c2, tube_on);
        float d = RunDelay(&sd, string ? x * 0.5f : x, time[i], regen, freeze,
                           ks_damp, diffuse);
        y = string ? d * 2.0f : x + d * echo;
      } else {
        float d = RunDelay(&sd, string ? x * 0.5f : x, time[i], regen, freeze,
                           ks_damp, diffuse);
        x = string ? d : x + d * echo;
        x = sd.tube.Process(x, drive_, gain, makeup, miller_a, c2, tube_on);
        y = sd.filter.Process(x, g_[i], k, morph_);
      }
      io[i] = y;
      if (cross) {
        // 20 Hz high-pass, then tanh: what goes to the other side.
        const float hp = y - sd.xfb_x1 + xfb_r * sd.xfb_y1;
        sd.xfb_x1 = y;
        sd.xfb_y1 = hp;
        sd.xfb = FastTanh(hp);
      }
    }
    out[s].l = io[0];
    out[s].r = io[1];
  }
  if (cross) cross_ = cross_target;
  if (cross_target == 0.0f) {
    cross_ = 0.0f;
    for (int i = 0; i < 2; ++i) {
      side_[i].xfb = side_[i].xfb_x1 = side_[i].xfb_y1 = 0.0f;
    }
  }
}

}  // namespace monsoon
