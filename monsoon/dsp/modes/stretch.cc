// Mode 7: STRETCH (see stretch.h, docs/DESIGN.md).

#include "monsoon/dsp/modes/stretch.h"

#include "monsoon/dsp/stft.h"

namespace monsoon {

static const size_t kMaxBufferBytes = 118 * 1024;

float StretchMode::AdvancePerHop(float size) {
  const float hop = static_cast<float>(kHop);
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
  ring_size_ = kFftSize + 2 * kHop;
  // Biggest fixed allocations first.
  for (int ch = 0; ch < 2; ++ch) {
    ring_[ch] = NULL;
    ref_phase_[ch] = synth_phase_[ch] = NULL;
    inst_[ch] = smear_[ch] = NULL;
  }
  for (int ch = 0; ch < channels_; ++ch) {
    ring_[ch] = arena->Allocate<float>(ring_size_);
    if (!ring_[ch]) return false;
  }
  frame_ = arena->Allocate<float>(kFftSize);
  spectrum_ = arena->Allocate<float>(kFftSize);
  if (!frame_ || !spectrum_) return false;
  for (int ch = 0; ch < channels_; ++ch) {
    inst_[ch] = arena->Allocate<float>(kBins);
    smear_[ch] = arena->Allocate<float>(kBins);
    ref_phase_[ch] = arena->Allocate<uint16_t>(kBins);
    synth_phase_[ch] = arena->Allocate<uint16_t>(kBins);
    if (!inst_[ch] || !smear_[ch] || !ref_phase_[ch] || !synth_phase_[ch]) {
      return false;
    }
    for (size_t k = 0; k < kBins; ++k) {
      inst_[ch][k] = static_cast<float>(k) / kFftSize;
    }
  }
  if (!buffer_.Init(arena, kMaxBufferBytes, channels_)) return false;

  memset(&controls_, 0, sizeof(controls_));
  controls_.blend = 1.0f;
  rng_.Seed(0x3c6ef372u);
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
  if (!c.freeze) {
    for (size_t i = 0; i + kBlockSize <= n; i += kBlockSize) {
      buffer_.WriteBlock(in + i);
    }
    written_ += static_cast<uint32_t>(n);
  }
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
}

void StretchMode::Prepare() {
  if (hops_done_ == hops_ready_) return;
  if (hops_ready_ - hops_done_ > 2) hops_done_ = hops_ready_ - 1;
  const size_t end = (kHop * (hops_done_ + 1)) % ring_size_;
  ++hops_done_;

  const Controls c = controls_;
  const float n = static_cast<float>(kFftSize);
  const float hop = static_cast<float>(kHop);
  const float advance = AdvancePerHop(c.size);

  // Read window placement, as a delay (frame end behind the write head).
  const float filled = static_cast<float>(buffer_.filled());
  const float d_lo = c.freeze ? 4.0f : 2.0f * hop + 64.0f;
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
  float d = d_lo + c.position * span + drift_;
  while (d > d_hi) { drift_ -= span; d -= span; }
  while (d < d_lo) { drift_ += span; d += span; }

  const uint32_t written = written_;
  const int32_t start = static_cast<int32_t>(buffer_.write_head()) -
      static_cast<int32_t>(d + 0.5f) - static_cast<int32_t>(kFftSize);

  // Distance the analysis window moved since the phase reference, in
  // samples of recorded content.
  float dist = static_cast<float>(written - ref_written_) - (d - ref_delay_);
  bool reset = !have_ref_ || dist < 0.0f || dist > 1536.0f;
  // Standing still (infinite stretch): no motion to measure frequencies
  // from, so take an extra reference frame 16 samples earlier now and then.
  if (have_ref_ && dist < 4.0f && dist > -4.0f && hops_since_ref_ >= 24) {
    for (int ch = 0; ch < channels_; ++ch) AnalyseReference(ch, start - 16);
    ref_written_ = written;
    ref_delay_ = d + 16.0f;
    dist = 16.0f;
    hops_since_ref_ = 24;
  }
  // Prefer a long baseline (accurate); at extreme stretch factors the read
  // window may wrap before travelling far, so accept a short baseline once
  // the reference has aged.
  ++hops_since_ref_;
  bool update = !reset &&
      (dist >= 96.0f || (dist >= 4.0f && hops_since_ref_ >= 24));
  if (reset || update) {
    hops_since_ref_ = 0;
    ref_written_ = written;
    ref_delay_ = d;
    have_ref_ = true;
  }
  for (int ch = 0; ch < channels_; ++ch) {
    ProcessChannel(ch, start, dist, update, reset);
    // Overlap-add.
    const float gain = hop / n * 2.0f / n;
    const float inv_n = 1.0f / n;
    float* o = ring_[ch];
    for (size_t i = 0; i < kFftSize; ++i) {
      size_t j = out_start + i;
      if (j >= ring_size_) j -= ring_size_;
      o[j] += spectrum_[i] * gain * FastSin(0.5f * static_cast<float>(i) * inv_n);
    }
  }
}

void StretchMode::AnalyseReference(int ch, int32_t start) {
  const float inv_n = 1.0f / static_cast<float>(kFftSize);
  for (size_t i = 0; i < kFftSize; ++i) {
    frame_[i] = buffer_.At(ch, start + static_cast<int32_t>(i)) *
        FastSin(0.5f * static_cast<float>(i) * inv_n);
  }
  FftDirect(frame_, spectrum_, kFftSize);
  const float* re = spectrum_;
  const float* im = spectrum_ + kBins;
  uint16_t* ref = ref_phase_[ch];
  for (size_t k = 1; k < kBins; ++k) {
    float ph = FastAtan2Cycles(-im[k], re[k]);
    ph -= floorf(ph);
    ref[k] = static_cast<uint16_t>(static_cast<int32_t>(ph * 65536.0f) & 0xffff);
  }
}

void StretchMode::ProcessChannel(int ch, int32_t start, float dist,
                                 bool update_ref, bool reset_ref) {
  const Controls& c = controls_;
  const float n = static_cast<float>(kFftSize);
  const float inv_n = 1.0f / n;
  // Analysis.
  for (size_t i = 0; i < kFftSize; ++i) {
    frame_[i] = buffer_.At(ch, start + static_cast<int32_t>(i)) *
        FastSin(0.5f * static_cast<float>(i) * inv_n);
  }
  FftDirect(frame_, spectrum_, kFftSize);

  float* re = spectrum_;
  float* im = spectrum_ + kBins;
  float* inst = inst_[ch];
  float* smear = smear_[ch];
  uint16_t* ref = ref_phase_[ch];
  uint16_t* synth = synth_phase_[ch];
  const float smooth = 1.0f - Clamp(c.density, 0.0f, 1.0f) * 0.97f;
  const float inv_dist = update_ref ? 1.0f / dist : 0.0f;

  for (size_t k = 1; k < kBins; ++k) {
    float mag = sqrtf(re[k] * re[k] + im[k] * im[k]);
    // ShyFFT's imaginary part has the opposite sign of the usual e^{-jwt}
    // convention, so negate it to get phases that advance with time.
    float ph = FastAtan2Cycles(-im[k], re[k]);
    if (update_ref) {
      float prev = static_cast<float>(ref[k]) * (1.0f / 65536.0f);
      float dev = ph - prev - static_cast<float>(k) * dist * inv_n;
      dev -= floorf(dev + 0.5f);
      inst[k] = static_cast<float>(k) * inv_n + dev * inv_dist;
    }
    if (update_ref || reset_ref) {
      float p = ph - floorf(ph);
      ref[k] = static_cast<uint16_t>(static_cast<int32_t>(p * 65536.0f) & 0xffff);
    }
    smear[k] += smooth * (mag - smear[k]);
    re[k] = ph;  // keep the analysis phase for phase locking below
  }

  // Magnitudes (with optional bin blur) into im[], reused as scratch.
  const float blur = c.density > 0.5f ? (c.density - 0.5f) * 2.0f : 0.0f;
  float* mags = im;
  for (size_t k = 1; k < kBins; ++k) {
    float m = smear[k];
    if (blur > 0.0f && k > 1 && k + 1 < kBins) {
      float b = 0.25f * smear[k - 1] + 0.5f * smear[k] + 0.25f * smear[k + 1];
      m += blur * (b - m);
    }
    mags[k] = m;
  }
  mags[0] = 0.0f;

  // Synthesis with identity phase locking (Laroche & Dolson): each region
  // around a spectral peak keeps its analysed phase relationships and
  // rotates as a whole with the peak's running phase.
  memset(frame_, 0, sizeof(float) * kFftSize);
  float* ore = frame_;
  float* oim = frame_ + kBins;
  const float ratio = SemitonesToRatio(c.pitch);
  const float randomness = Clamp(c.texture, 0.0f, 1.0f) * 0.5f;
  const float hop = static_cast<float>(kHop);
  int last_j = -1;
  size_t region = 1;
  while (region < kBins) {
    // Region ends at the next valley.
    size_t end = region + 1;
    while (end < kBins - 1 &&
           !(mags[end] < mags[end - 1] && mags[end] <= mags[end + 1])) {
      ++end;
    }
    if (end >= kBins - 1) end = kBins;
    size_t peak = region;
    for (size_t k = region; k < end; ++k) {
      if (mags[k] > mags[peak]) peak = k;
    }
    int jp = static_cast<int>(static_cast<float>(peak) * ratio + 0.5f);
    if (jp >= 1 && jp < static_cast<int>(kBins) && jp != last_j) {
      float inc = inst[peak] * ratio * hop;
      inc -= floorf(inc);
      synth[jp] = static_cast<uint16_t>(synth[jp] +
          static_cast<uint16_t>(static_cast<int32_t>(inc * 65536.0f) & 0xffff));
      last_j = jp;
    }
    const float peak_phase = jp >= 1 && jp < static_cast<int>(kBins)
        ? static_cast<float>(synth[jp]) * (1.0f / 65536.0f) : 0.0f;
    const float peak_analysis = re[peak];
    for (size_t k = region; k < end; ++k) {
      int j = static_cast<int>(static_cast<float>(k) * ratio + 0.5f);
      if (j < 1 || j >= static_cast<int>(kBins)) continue;
      float phase = peak_phase + (re[k] - peak_analysis);
      if (randomness > 0.0f) phase += rng_.Bipolar() * randomness;
      float m = mags[k];
      ore[j] += m * FastCos(phase);
      oim[j] -= m * FastSin(phase);
    }
    region = end;
  }
  FftInverse(frame_, spectrum_, kFftSize);
}

}  // namespace monsoon
