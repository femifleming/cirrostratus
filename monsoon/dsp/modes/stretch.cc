// Mode 7: STRETCH (see stretch.h, docs/DESIGN.md).

#include "monsoon/dsp/modes/stretch.h"

#include "monsoon/dsp/modes/spectral_util.h"

#include "monsoon/dsp/stft.h"

namespace monsoon {

// Arena::Allocate out of line: Init() calls it many times and flash is
// tight (same allocations, same order).
static __attribute__((noinline)) void* Grab(Arena* arena, size_t bytes) {
  return arena->Allocate(bytes);
}
template<typename T> static inline T* Grab(Arena* arena, size_t n) {
  return static_cast<T*>(Grab(arena, n * sizeof(T)));
}

static const size_t kMaxBufferBytes = 118 * 1024;

float StretchMode::AdvancePerHop(float size, float hop) {
  if (size < 0.9f) {
    // 1x at 0 .. 1000x at 0.9 (exponential).
    return hop / exp2f(size * (9.965784f / 0.9f));
  }
  // 0.9 .. 1: from 1000x down to a standstill.
  float t = (size - 0.9f) * 10.0f;
  float a = hop * 0.001f * (1.0f - t * 1.25f);
  return a > 0.0f ? a : 0.0f;
}

bool StretchMode::Init(Arena* arena) {
  channels_ = init_option_ == 1 ? 1 : 2;
  kFftSize = FftSize();
  kHop = (kFftSize + 2) / 3;           // 683 at 2048
  kBins = kFftSize / 2;
  kTopBin = kFftSize * 854 / 2048;     // 20.0 kHz
  frame_ratio_ = static_cast<float>(kHop) / static_cast<float>(kNativeHop);
  ref_hops_long_ = static_cast<uint32_t>(96.0f / frame_ratio_ + 0.5f);
  ref_hops_short_ = static_cast<uint32_t>(24.0f / frame_ratio_ + 0.5f);
  max_baseline_ = 1536.0f * static_cast<float>(kFftSize) / 2048.0f;
  ring_size_ = kFftSize + 2 * kHop;
  // Biggest fixed allocations first.
  for (int ch = 0; ch < 2; ++ch) {
    ring_[ch] = NULL;
    ref_[ch] = NULL;
    synth_phase_[ch] = NULL;
    inst_[ch] = smear_[ch] = NULL;
  }
  for (int ch = 0; ch < channels_; ++ch) {
    ring_[ch] = Grab<float>(arena, ring_size_);
    if (!ring_[ch]) return false;
  }
  frame_ = Grab<float>(arena, kFftSize);
  spectrum_ = Grab<float>(arena, kFftSize);
  if (!frame_ || !spectrum_) return false;
  for (int ch = 0; ch < channels_; ++ch) {
    inst_[ch] = Grab<float>(arena, kBins);
    smear_[ch] = Grab<float>(arena, kBins);
    ref_[ch] = Grab<int16_t>(arena, 2 * kBins);
    synth_phase_[ch] = Grab<uint16_t>(arena, kBins);
    if (!inst_[ch] || !smear_[ch] || !ref_[ch] || !synth_phase_[ch]) {
      return false;
    }
    for (size_t k = 0; k < kBins; ++k) {
      inst_[ch][k] = static_cast<float>(k) / kFftSize;
    }
  }
  if (!buffer_.Init(arena, kMaxBufferBytes, channels_, 0, buffer_quality_)) {
    return false;
  }

  memset(&controls_, 0, sizeof(controls_));
  controls_.blend = 1.0f;
  rng_.Seed(0x3c6ef372u);
  window_cos_ = cosf(kPi / kFftSize);
  window_sin_ = sinf(kPi / kFftSize);
  pos_ = 0;
  hop_counter_ = 0;
  hops_ready_ = 0;
  hops_done_ = 0;
  written_ = 0;
  jump_pending_ = false;
  previous_trigger_ = false;
  drift_ = 0.0f;
  ref_written_ = 0;
  ref_delay_ = 0.0f;
  have_ref_ = false;
  hops_since_ref_ = 0;
  return true;
}

void StretchMode::Process(const Controls& c, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  controls_ = c;
  if (c.trigger && !previous_trigger_) jump_pending_ = true;
  previous_trigger_ = c.trigger;
  const bool stereo = channels_ == 2;
  for (size_t i = 0; i < n; ++i) {
    float l = ring_[0][pos_];
    ring_[0][pos_] = 0.0f;
    float r = l;
    if (stereo) {
      r = ring_[1][pos_];
      ring_[1][pos_] = 0.0f;
    }
    out[i].l = l;
    out[i].r = r;
    if (++pos_ >= ring_size_) pos_ = 0;
    if (++hop_counter_ >= kHop) {
      hop_counter_ = 0;
      ++hops_ready_;
    }
  }
  if (!c.freeze) {
    // FBK: the output goes back into the recording with the input. The
    // input is trimmed so the (incoherent) sum keeps the input's power.
    const float fb = Clamp(c.fbk, 0.0f, 1.0f) * 0.85f;
    const float trim = fb > 0.0f ? sqrtf(1.0f - fb * fb) : 1.0f;
    FloatFrame mix[kBlockSize];
    for (size_t i = 0; i + kBlockSize <= n; i += kBlockSize) {
      const FloatFrame* src = in + i;
      if (fb > 0.0f) {
        for (size_t j = 0; j < kBlockSize; ++j) {
          mix[j].l = SoftClip(in[i + j].l * trim + fb * out[i + j].l);
          mix[j].r = SoftClip(in[i + j].r * trim + fb * out[i + j].r);
        }
        src = mix;
      }
      buffer_.WriteBlock(src);
    }
    written_ += static_cast<uint32_t>(n);
  }
}

void StretchMode::Prepare() {
  if (hops_done_ == hops_ready_) return;
  if (hops_ready_ - hops_done_ > 2) hops_done_ = hops_ready_ - 1;
  const size_t end = (kHop * (hops_done_ + 1)) % ring_size_;
  ++hops_done_;

  const Controls c = controls_;
  const float n = static_cast<float>(kFftSize);
  const float hop = static_cast<float>(kHop);
  // BAL left scales the read speed from 1 through 0 (9 o'clock) to -1.
  const float bal = KnobBipolar(c.bal);
  const float advance = bal < 0.0f ? AdvancePerHop(c.size, hop) * (1.0f + 2.0f * bal)
                                   : AdvancePerHop(c.size, hop);

  // Read window placement, as a delay (frame end behind the write head).
  const float filled = static_cast<float>(buffer_.filled());
  // Lower stored rates: wider margins, and live reads stay behind the
  // newest stored block (both 0 extra at 48 kHz).
  const float m = static_cast<float>(buffer_.decimation());
  const float d_lo = (c.freeze ? 4.0f * m : 2.0f * hop + 64.0f) +
      static_cast<float>(buffer_.guard());
  const float d_hi = filled - n - 64.0f;
  const float span = d_hi - d_lo;
  const size_t out_start = (end + kHop) % ring_size_;
  if (span < hop) {
    return;  // Not enough audio recorded yet: stay silent.
  }
  if (c.freeze) {
    drift_ -= advance;          // write head still: move toward newer audio
  } else {
    drift_ += hop - advance;    // write head moves on: fall behind
  }
  if (jump_pending_) {
    jump_pending_ = false;
    // Newest audio. When frozen the read window moves toward newer audio
    // (and wraps to the oldest), so land a little before the very end.
    float back = c.freeze ? (span * 0.25f < 12000.0f ? span * 0.25f : 12000.0f)
                          : 0.0f;
    drift_ = -c.position * span + back;
    have_ref_ = false;
  }
  // BAL right: scatter, now and then jump somewhere else (no random
  // numbers are drawn at noon or left of it).
  if (bal > 0.0f && rng_.Float() < bal * 0.15f) {
    drift_ += rng_.Bipolar() * bal * 0.5f * span;
    have_ref_ = false;
  }
  float d = d_lo + c.position * span + drift_;
  while (d > d_hi) { drift_ -= span; d -= span; }
  while (d < d_lo) { drift_ += span; d += span; }

  const uint32_t written = written_;
  const int32_t start = static_cast<int32_t>(buffer_.head()) -
      static_cast<int32_t>(d + 0.5f) - static_cast<int32_t>(kFftSize);

  // Distance the analysis window moved since the phase reference, in
  // samples of recorded content.
  float dist = static_cast<float>(written - ref_written_) - (d - ref_delay_);
  // Reading backwards (BAL left) the content distance runs negative: the
  // checks below use its size, the frequency estimate its sign.
  const float adist = advance < 0.0f ? -dist : dist;
  bool reset = !have_ref_ || adist < 0.0f || adist > max_baseline_;
  // Standing still (infinite stretch): no motion to measure frequencies
  // from, so take an extra reference frame 16 samples earlier now and then.
  if (have_ref_ && dist < 4.0f && dist > -4.0f &&
      hops_since_ref_ >= ref_hops_long_) {
    for (int ch = 0; ch < channels_; ++ch) {
      Analyse(ch, start - 16);
      StoreReference(ch);
    }
    ref_written_ = written;
    ref_delay_ = d + 16.0f;
    dist = 16.0f;
    hops_since_ref_ = ref_hops_long_;
  }
  // Prefer a long baseline (accurate); at extreme stretch factors the read
  // window may wrap before travelling far, so accept a short baseline once
  // the reference has aged.
  ++hops_since_ref_;
  bool update = !reset &&
      (adist >= 96.0f || (adist >= 4.0f && hops_since_ref_ >= ref_hops_short_));
  if (reset || update) {
    hops_since_ref_ = 0;
    ref_written_ = written;
    ref_delay_ = d;
    have_ref_ = true;
  }
  for (int ch = 0; ch < channels_; ++ch) {
    Analyse(ch, start);
    Synthesize(ch, dist, update, reset);
    OverlapAdd(ch, out_start);
  }
}

// Fills frame_ with the sine-windowed buffer segment and transforms it.
// The window sin(pi i / N) comes from a rotating phasor (no table lookups).
void StretchMode::Analyse(int ch, int32_t start) {
  // The window is symmetric (w[i] == w[N - i]), so each phasor step serves
  // two samples: i walking forward and N - i walking backward.
  const int32_t size = static_cast<int32_t>(buffer_.size());
  int32_t a = start % size;
  if (a < 0) a += size;
  int32_t b = a + static_cast<int32_t>(kFftSize / 2);
  if (b >= size) b -= size;
  float ws = window_sin_, wc = window_cos_;
  const float rc = window_cos_, rs = window_sin_;
  if (buffer_.lofi()) {
    // Lower buffer quality: read the window in one pass, then shape it
    // with the same phasor (w[0] = 0, w[N/2] = 1).
    buffer_.ReadSpan(ch, a, kFftSize, frame_);
    frame_[0] = 0.0f;
    for (size_t i = 1; i < kFftSize / 2; ++i) {
      frame_[i] *= ws;
      frame_[kFftSize - i] *= ws;
      float ns = ws * rc + wc * rs;
      wc = wc * rc - ws * rs;
      ws = ns;
    }
    FftDirect(frame_, spectrum_, kFftSize);
    return;
  }
  // i = 0 (window 0) and i = N/2 (window 1).
  frame_[0] = 0.0f;
  frame_[kFftSize / 2] = buffer_.SampleFast(ch, static_cast<size_t>(b));
  int32_t fwd = a + 1;
  if (fwd >= size) fwd -= size;
  int32_t back = a + static_cast<int32_t>(kFftSize) - 1;
  if (back >= size) back -= size;
  for (size_t i = 1; i < kFftSize / 2; ++i) {
    frame_[i] = buffer_.SampleFast(ch, static_cast<size_t>(fwd)) * ws;
    frame_[kFftSize - i] = buffer_.SampleFast(ch, static_cast<size_t>(back)) * ws;
    if (++fwd == size) fwd = 0;
    if (--back < 0) back = size - 1;
    float ns = ws * rc + wc * rs;
    wc = wc * rc - ws * rs;
    ws = ns;
  }
  FftDirect(frame_, spectrum_, kFftSize);
}

// Stores the unit vector of every bin of spectrum_ as the phase reference.
void StretchMode::StoreReference(int ch) {
  const float* re = spectrum_;
  const float* im = spectrum_ + kBins;
  int16_t* ref = ref_[ch];
  for (size_t k = 1; k < kTopBin; ++k) {
    float m = sqrtf(re[k] * re[k] + im[k] * im[k]);
    float s = 32767.0f / (m + 1e-20f);
    // ShyFFT's imaginary part is negated: store the textbook phasor.
    ref[2 * k] = static_cast<int16_t>(re[k] * s);
    ref[2 * k + 1] = static_cast<int16_t>(-im[k] * s);
  }
}

void StretchMode::OverlapAdd(int ch, size_t out_start) {
  const float n = static_cast<float>(kFftSize);
  const float gain = static_cast<float>(kHop) / n * 2.0f / n;
  float* o = ring_[ch];
  const size_t half = kFftSize / 2;
  size_t mid = out_start + half;
  if (mid >= ring_size_) mid -= ring_size_;
  o[mid] += spectrum_[half] * gain;
  // Symmetric window again: one phasor step for samples i and N - i.
  float ws = window_sin_ * gain, wc = window_cos_ * gain;
  const float rc = window_cos_, rs = window_sin_;
  size_t fwd = out_start + 1;
  if (fwd >= ring_size_) fwd -= ring_size_;
  size_t back = out_start + kFftSize - 1;
  if (back >= ring_size_) back -= ring_size_;
  for (size_t i = 1; i < half; ++i) {
    o[fwd] += spectrum_[i] * ws;
    o[back] += spectrum_[kFftSize - i] * ws;
    if (++fwd == ring_size_) fwd = 0;
    back = back == 0 ? ring_size_ - 1 : back - 1;
    float ns = ws * rc + wc * rs;
    wc = wc * rc - ws * rs;
    ws = ns;
  }
}

// Phase-locked vocoder resynthesis (Laroche & Dolson identity locking),
// done with complex rotations: per region around a spectral peak, one
// atan2 and one sin/cos; every bin is then rotated by the peak's phase
// correction. Paulstretch randomness is a table-driven random rotation.
void StretchMode::Synthesize(int ch, float dist, bool update_ref,
                             bool reset_ref) {
  const Controls& c = controls_;
  const float n = static_cast<float>(kFftSize);
  const float inv_n = 1.0f / n;
  const float* re = spectrum_;
  const float* im = spectrum_ + kBins;
  float* inst = inst_[ch];
  float* smear = smear_[ch];
  int16_t* ref = ref_[ch];
  uint16_t* synth = synth_phase_[ch];

  // Magnitude smear (DENSITY).
  const float smooth = frame_ratio_ == 1.0f
      ? 1.0f - Clamp(c.density, 0.0f, 1.0f) * 0.97f
      : 1.0f - spectral_util::PerFrame(Clamp(c.density, 0.0f, 1.0f) * 0.97f,
                                       frame_ratio_);
  for (size_t k = 1; k < kTopBin; ++k) {
    float mag = sqrtf(re[k] * re[k] + im[k] * im[k]);
    smear[k] += smooth * (mag - smear[k]);
  }
  smear[0] = 0.0f;
  smear[kTopBin] = 0.0f;

  memset(frame_, 0, sizeof(float) * kFftSize);
  float* ore = frame_;
  float* oim = frame_ + kBins;
  const float ratio = SemitonesToRatio(c.pitch);
  const float blur = c.density > 0.5f ? (c.density - 0.5f) * 2.0f : 0.0f;
  const float randomness = Clamp(c.texture, 0.0f, 1.0f);
  const float halo = Clamp(c.rvb, 0.0f, 1.0f) * 0.7f;
  const int half_top = static_cast<int>(kTopBin) / 2;
  const float hop = static_cast<float>(kHop);
  const float inv_dist = update_ref ? 1.0f / dist : 0.0f;
  const bool store_ref = update_ref || reset_ref;
  const int top = static_cast<int>(kTopBin);
  int last_j = -1;
  Random rng = rng_;  // local copy keeps the state in registers
  size_t region = 1;
  while (region < kTopBin) {
    // A region runs up to the next valley of the smeared magnitudes, and
    // spans at least 4 bins (noise would otherwise make a region of every
    // other bin, each costing a sin/cos).
    size_t end = region + 1;
    size_t peak = region;
    while (end < kTopBin &&
           (end - region < 4 ||
            !(smear[end] < smear[end - 1] && smear[end] <= smear[end + 1]))) {
      if (smear[end] > smear[peak]) peak = end;
      ++end;
    }
    // Peak phase (textbook sign) and frequency.
    const float pre = re[peak], pim = -im[peak];
    if (update_ref) {
      const float rr = ref[2 * peak], ri = ref[2 * peak + 1];
      // arg(X * conj(ref))
      float dphi = FastAtan2Cycles(pim * rr - pre * ri, pre * rr + pim * ri);
      float dev = dphi - static_cast<float>(peak) * dist * inv_n;
      dev -= FastFloor(dev + 0.5f);
      const float f = static_cast<float>(peak) * inv_n + dev * inv_dist;
      for (size_t k = region; k < end; ++k) inst[k] = f;
    }
    int jp = static_cast<int>(static_cast<float>(peak) * ratio + 0.5f);
    float rot_c = 1.0f, rot_s = 0.0f;
    if (jp >= 1 && jp < top) {
      if (jp != last_j) {
        float inc = inst[peak] * ratio * hop;
        inc -= FastFloor(inc);
        synth[jp] = static_cast<uint16_t>(synth[jp] +
            static_cast<uint16_t>(static_cast<int32_t>(inc * 65536.0f) & 0xffff));
        last_j = jp;
      }
      // e^{i theta} = e^{i synth} * conj(X_peak) / |X_peak|
      const float sp = static_cast<float>(synth[jp]) * (1.0f / 65536.0f);
      const float sc = FastCos(sp), ss = FastSin(sp);
      const float inv = 1.0f / (sqrtf(pre * pre + pim * pim) + 1e-20f);
      const float ur = pre * inv, ui = -pim * inv;
      rot_c = sc * ur - ss * ui;
      rot_s = sc * ui + ss * ur;
    }
    for (size_t k = region; k < end; ++k) {
      const float xr = re[k], xi = -im[k];
      const float mag = sqrtf(xr * xr + xi * xi);
      const float inv = 1.0f / (mag + 1e-20f);
      if (store_ref) {
        ref[2 * k] = static_cast<int16_t>(xr * inv * 32767.0f);
        ref[2 * k + 1] = static_cast<int16_t>(xi * inv * 32767.0f);
      }
      int j = static_cast<int>(static_cast<float>(k) * ratio + 0.5f);
      if (j < 1 || j >= top) continue;
      float m = smear[k];
      if (blur > 0.0f) {
        float b = 0.25f * (smear[k - 1] + smear[k + 1]) + 0.5f * m;
        m += blur * (b - m);
      }
      const float g = m * inv;
      // Y = X * g * e^{i theta} (* a random rotation for Paulstretch).
      float gc = g * rot_c, gs = g * rot_s;
      if (randomness > 0.0f) {
        // Uniform +/- half a turn, scaled by TEXTURE, from 10 random bits.
        int32_t a = static_cast<int32_t>(
            static_cast<float>(static_cast<int32_t>(rng.Next()) >> 22) *
            randomness) & 1023;
        const float cr = kSineTable[(a + 256) & 1023], sr = kSineTable[a];
        float t = gc * cr - gs * sr;
        gs = gc * sr + gs * cr;
        gc = t;
      }
      const float a = xr * gc - xi * gs, b = xr * gs + xi * gc;
      ore[j] += a;
      oim[j] -= b;  // back to ShyFFT's sign
      if (halo > 0.0f && j < half_top) {
        // RVB: the octave, Y^2 / |Y| (doubled phase, same magnitude).
        const float h = halo / (m + 1e-20f);
        ore[2 * j] += (a * a - b * b) * h;
        oim[2 * j] -= 2.0f * a * b * h;
      }
    }
    region = end;
  }
  rng_ = rng;
  FftInverse(frame_, spectrum_, kFftSize);
}

}  // namespace monsoon
