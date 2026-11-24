// Mode 19 - HARMONIZER (see harmonizer.h).

#include "monsoon/dsp/modes/harmonizer.h"

namespace monsoon {

namespace {

const int kScaleLength[4] = { 7, 7, 7, 5 };
const int kScales[4][7] = {
  { 0, 2, 4, 5, 7, 9, 11 },  // major
  { 0, 2, 3, 5, 7, 8, 10 },  // natural minor
  { 0, 2, 3, 5, 7, 9, 10 },  // dorian
  { 0, 2, 4, 7, 9, 0, 0 },   // major pentatonic
};

struct Step {
  int8_t steps;    // along the scale
  int8_t octaves;  // whole octaves on top
};
const int kSets = 8;
const int kSetSize[kSets] = { 1, 1, 2, 2, 2, 2, 3, 3 };
const Step kSetSteps[kSets][3] = {
  { { 2, 0 } },                          // 3rd up
  { { 4, 0 } },                          // 5th up
  { { 2, 0 }, { 4, 0 } },                // 3rd + 5th
  { { 0, 1 }, { 0, -1 } },               // octave up + down
  { { -2, 0 }, { 2, 0 } },               // 3rd below + above
  { { 3, 0 }, { 6, 0 } },                // 4th + 7th
  { { 2, 0 }, { 4, 0 }, { 0, 1 } },      // triad + octave
  { { 4, 0 }, { 1, 1 }, { 0, -1 } },     // 5th, 9th, octave down
};

inline int FloorDiv(int a, int b) {
  int q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

const float kWindowMin = 960.0f;   // 20 ms
const float kWindowMax = 2880.0f;  // 60 ms
const float kTapBase = 32.0f;

}  // namespace

float HarmonizerMode::TargetNote(float input_note, int root, int scale,
                                 int set, int voice, int* count) {
  scale &= 3;
  set = ((set % kSets) + kSets) % kSets;
  const int len = kScaleLength[scale];
  const int* sc = kScales[scale];
  if (count) *count = kSetSize[set];
  // Snap the input to the nearest scale degree.
  const float n = input_note - static_cast<float>(root);
  int octave = static_cast<int>(FastFloor(n / 12.0f));
  float rem = n - 12.0f * octave;
  int best = 0;
  float best_d = 100.0f;
  for (int d = 0; d <= len; ++d) {
    float pc = d < len ? static_cast<float>(sc[d]) : 12.0f;
    float dist = fabsf(rem - pc);
    if (dist < best_d) { best_d = dist; best = d; }
  }
  int degree = octave * len + best;  // index along the scale
  if (voice >= kSetSize[set]) voice = kSetSize[set] - 1;
  const Step s = kSetSteps[set][voice];
  const int t = degree + s.steps;
  const int oo = FloorDiv(t, len);
  const int dd = t - oo * len;
  return static_cast<float>(root + 12 * (oo + s.octaves) + sc[dd]);
}

bool HarmonizerMode::Init(Arena* arena) {
  ring_ = arena->Allocate<float>(kRingSize);
  dec_ = arena->Allocate<float>(kDecSize);
  nsdf_ = arena->Allocate<float>(kWindow + kMaxLag + kMaxLag + 8);
  if (!ring_ || !dec_ || !nsdf_) return false;
  write_ = 0;
  dec_write_ = 0;
  dec_acc_ = 0.0f;
  dec_count_ = 0;
  dec_hops_ = 0;
  dec_hops_done_ = 0;
  tracked_hz_ = 220.0f;
  voiced_ = false;
  period_ = 48000.0f / 220.0f;
  for (int v = 0; v < kVoices; ++v) {
    phase_[v] = 0.5f * v / kVoices;
    log_ratio_[v] = 0.0f;
    latched_note_[v] = 60.0f;
    gain_[v] = 0.0f;
  }
  latched_ = false;
  was_trigger_ = false;
  set_offset_ = 0;
  return true;
}

void HarmonizerMode::Prepare() {
  if (dec_hops_done_ == dec_hops_) return;
  dec_hops_done_ = dec_hops_;
  Analyse();
}

// McLeod pitch method on the newest kWindow + kMaxLag decimated samples.
void HarmonizerMode::Analyse() {
  const size_t total = kWindow + kMaxLag;
  float* x = nsdf_;                 // linear copy of the samples
  float* nsdf = nsdf_ + total;      // nsdf[lag]
  size_t start = (dec_write_ + kDecSize - total) & (kDecSize - 1);
  for (size_t i = 0; i < total; ++i) {
    x[i] = dec_[(start + i) & (kDecSize - 1)];
  }
  float e0 = 0.0f;
  for (size_t i = 0; i < kWindow; ++i) e0 += x[i] * x[i];
  if (e0 < 1e-6f * kWindow) {
    voiced_ = false;
    return;
  }
  // Energy of the lagged window, slid along.
  float el = 0.0f;
  for (size_t i = 0; i < kWindow; ++i) {
    el += x[i + kMinLag] * x[i + kMinLag];
  }
  float max_val = 0.0f;
  for (int lag = kMinLag; lag <= kMaxLag; ++lag) {
    float r = 0.0f;
    const float* a = x;
    const float* b = x + lag;
    for (size_t i = 0; i < kWindow; i += 4) {
      r += a[i] * b[i] + a[i + 1] * b[i + 1] + a[i + 2] * b[i + 2] +
          a[i + 3] * b[i + 3];
    }
    float v = 2.0f * r / (e0 + el + 1e-12f);
    nsdf[lag] = v;
    if (v > max_val) max_val = v;
    el += x[lag + kWindow] * x[lag + kWindow] - x[lag] * x[lag];
  }
  if (max_val < 0.6f) {
    voiced_ = false;
    return;
  }
  // First key maximum above 0.88 x the highest one.
  const float threshold = 0.88f * max_val;
  int pick = -1;
  for (int lag = kMinLag + 1; lag < kMaxLag; ++lag) {
    if (nsdf[lag] >= threshold && nsdf[lag] >= nsdf[lag - 1] &&
        nsdf[lag] >= nsdf[lag + 1]) {
      pick = lag;
      break;
    }
  }
  if (pick < 0) {
    voiced_ = false;
    return;
  }
  // Parabolic interpolation.
  const float a = nsdf[pick - 1], b = nsdf[pick], c = nsdf[pick + 1];
  const float den = a - 2.0f * b + c;
  float delta = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
  delta = Clamp(delta, -0.5f, 0.5f);
  const float lag = static_cast<float>(pick) + delta;
  tracked_hz_ = 12000.0f / lag;
  voiced_ = true;
}

void HarmonizerMode::Process(const Controls& c, const FloatFrame* in,
                             FloatFrame* out, size_t n) {
  // ---- Input ring + decimated copy for the tracker.
  for (size_t i = 0; i < n; ++i) {
    const float m = 0.5f * (in[i].l + in[i].r);
    ring_[write_] = m;
    write_ = (write_ + 1) & (kRingSize - 1);
    dec_acc_ += m;
    if (++dec_count_ == 4) {
      dec_[dec_write_] = dec_acc_ * 0.25f;
      dec_write_ = (dec_write_ + 1) & (kDecSize - 1);
      dec_acc_ = 0.0f;
      dec_count_ = 0;
      if ((dec_write_ & 127) == 0) ++dec_hops_;
    }
  }

  // ---- Block-rate targets.
  const float hz = tracked_hz_;
  const float input_note = 69.0f + 12.0f * log2f(hz / 440.0f);
  const int root_semis = static_cast<int>(FastFloor(c.pitch + 0.5f));
  const int root = ((root_semis % 12) + 12) % 12;
  const int scale = c.option & 3;
  if (c.trigger && !was_trigger_) set_offset_ = (set_offset_ + 1) % kSets;
  was_trigger_ = c.trigger;
  int zone = static_cast<int>(c.position * kSets);
  if (zone >= kSets) zone = kSets - 1;
  const int set = (zone + set_offset_) % kSets;
  int count = 1;
  float target[kVoices];
  for (int v = 0; v < kVoices; ++v) {
    target[v] = TargetNote(input_note, root, scale, set, v, &count);
  }
  if (c.freeze && !latched_) {
    for (int v = 0; v < kVoices; ++v) latched_note_[v] = target[v];
  }
  latched_ = c.freeze;
  int active = 1 + static_cast<int>(c.density * 2.999f);
  if (active > count) active = count;
  const float level = 0.45f + 0.55f * c.density;
  static const float kDetune[kVoices] = { 1.0f, -1.0f, 0.6f };
  static const float kDelayShare[kVoices] = { 0.35f, 0.7f, 1.0f };
  static const float kPan[kVoices] = { -1.0f, 1.0f, -0.4f };

  // Pitch-synchronous window: the two taps sit W/2 apart, so W/2 must be a
  // whole number of input periods or they partly cancel (beating). Use the
  // smallest such W/2 of at least 10 ms.
  float target_period = 48000.0f / hz;
  period_ += 0.1f * (target_period - period_);
  float periods = FastFloor((0.5f * kWindowMin) / period_) + 1.0f;
  float window = 2.0f * periods * period_;
  while (window > kWindowMax && periods > 1.0f) {
    periods -= 1.0f;
    window = 2.0f * periods * period_;
  }
  window = Clamp(window, 64.0f, kWindowMax);

  float semis[kVoices];
  for (int v = 0; v < kVoices; ++v) {
    const float t = latched_ ? latched_note_[v] : target[v];
    float s = t - input_note + c.size * 0.15f * kDetune[v];
    s = Clamp(s, -36.0f, 36.0f);
    // Glide ~10 ms; jump if the gap is huge (new note far away).
    if (fabsf(s - log_ratio_[v]) > 7.0f && gain_[v] < 0.05f) {
      log_ratio_[v] = s;
    } else {
      log_ratio_[v] += 0.06f * (s - log_ratio_[v]);
    }
    semis[v] = log_ratio_[v];
  }

  // ---- Voices.
  float g_target[kVoices];
  float ratio[kVoices], inc[kVoices], base[kVoices], pl[kVoices], pr[kVoices];
  for (int v = 0; v < kVoices; ++v) {
    g_target[v] = v < active ? level : 0.0f;
    ratio[v] = SemitonesToRatio(semis[v]);
    inc[v] = (1.0f - ratio[v]) / window;
    base[v] = kTapBase + c.size * 960.0f * kDelayShare[v];
    const float pan = Clamp(c.texture * kPan[v], -1.0f, 1.0f);
    pl[v] = pan > 0.0f ? 1.0f - pan : 1.0f;
    pr[v] = pan < 0.0f ? 1.0f + pan : 1.0f;
  }
  const float mask = static_cast<float>(kRingSize);
  for (size_t i = 0; i < n; ++i) {
    // Position of this sample in the ring (the block is already written).
    const float now = static_cast<float>(
        (write_ + kRingSize - n + i) & (kRingSize - 1));
    float l = in[i].l, r = in[i].r;
    for (int v = 0; v < kVoices; ++v) {
      gain_[v] += 0.002f * (g_target[v] - gain_[v]);
      if (gain_[v] < 1e-4f && g_target[v] == 0.0f) continue;
      float ph = phase_[v] + inc[v];
      ph -= FastFloor(ph);
      phase_[v] = ph;
      float ph2 = ph + 0.5f;
      if (ph2 >= 1.0f) ph2 -= 1.0f;
      float y = 0.0f;
      const float phs[2] = { ph, ph2 };
      for (int tap = 0; tap < 2; ++tap) {
        float pos = now - (base[v] + phs[tap] * window);
        if (pos < 0.0f) pos += mask;
        const int32_t k = static_cast<int32_t>(pos);
        const float f = pos - static_cast<float>(k);
        const float xm1 = ring_[(k - 1) & (kRingSize - 1)];
        const float x0 = ring_[k & (kRingSize - 1)];
        const float x1 = ring_[(k + 1) & (kRingSize - 1)];
        const float x2 = ring_[(k + 2) & (kRingSize - 1)];
        const float w = FastSin(phs[tap] * 0.5f);
        y += Hermite(xm1, x0, x1, x2, f) * w * w;
      }
      y *= gain_[v];
      l += y * pl[v];
      r += y * pr[v];
    }
    out[i].l = l;
    out[i].r = r;
  }
}

}  // namespace monsoon
