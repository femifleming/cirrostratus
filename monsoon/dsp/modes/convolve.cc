// Mode 3 — CONVOLVE: convolution; FREEZE / TRIG capture the impulse response.

#include "monsoon/dsp/modes/convolve.h"

#include "monsoon/dsp/modes/spectral_util.h"
#include "monsoon/dsp/stft.h"

namespace monsoon {

using namespace spectral_util;

static const size_t B = ConvolveMode::kBlock;
static const size_t kFixedFloats =
    4 * B /* in */ + 4 * B /* out */ + 2 * B /* prev */ +
    3 * ConvolveMode::kFft /* work, spec, acc */;
static const size_t kPerPartitionBytes =
    3 * ConvolveMode::kFft * sizeof(int16_t) + B * sizeof(int16_t);
static const uint32_t kSaveMagic = 0x564E4F43;  // "CONV"

struct ConvolveSaveHeader {
  uint32_t magic;
  uint32_t partitions;
  ConvolveMode::IrParams params;
};

bool ConvolveMode::Init(Arena* arena) {
  // Size the IR to the memory that is left.
  size_t avail = arena->free(0) + arena->free(1);
  size_t fixed = kFixedFloats * sizeof(float) +
      (4 * kMaxPartitions) * sizeof(float) + 1024;
  if (avail < fixed + 4 * kPerPartitionBytes) return false;
  partitions_ = (avail - fixed) / kPerPartitionBytes;
  if (partitions_ > kMaxPartitions) partitions_ = kMaxPartitions;

  // Biggest first: the snapshot is one contiguous block.
  snapshot_ = arena->Allocate<int16_t>(partitions_ * B);
  if (!snapshot_) return false;
  for (size_t p = 0; p < partitions_; ++p) {
    fdl_[0][p] = arena->Allocate<int16_t>(kFft);
    fdl_[1][p] = arena->Allocate<int16_t>(kFft);
    ir_[p] = arena->Allocate<int16_t>(kFft);
    if (!fdl_[0][p] || !fdl_[1][p] || !ir_[p]) return false;
  }
  work_ = arena->Allocate<float>(kFft);
  spec_ = arena->Allocate<float>(kFft);
  acc_ = arena->Allocate<float>(kFft);
  for (int i = 0; i < 2; ++i) {
    for (int ch = 0; ch < 2; ++ch) {
      in_buf_[i][ch] = arena->Allocate<float>(B);
      out_buf_[i][ch] = arena->Allocate<float>(B);
      if (!in_buf_[i][ch] || !out_buf_[i][ch]) return false;
    }
    prev_[i] = arena->Allocate<float>(B);
    fdl_scale_[i] = arena->Allocate<float>(kMaxPartitions);
    if (!prev_[i] || !fdl_scale_[i]) return false;
  }
  ir_scale_ = arena->Allocate<float>(kMaxPartitions);
  snap_scale_ = arena->Allocate<float>(kMaxPartitions);
  if (!work_ || !spec_ || !acc_ || !ir_scale_ || !snap_scale_) return false;

  fill_ = 0;
  parity_ = 0;
  blocks_ready_ = 0;
  blocks_done_ = 0;
  prev_freeze_ = false;
  capture_request_ = false;
  memset(&ctl_, 0, sizeof(ctl_));
  ctl_.position = ctl_.size = ctl_.density = ctl_.texture = 0.5f;
  head_ = 0;
  active_partitions_ = 0;
  capture_left_ = 0;
  capture_head_ = 0;
  captures_ = 0;
  build_state_ = BUILD_IDLE;
  force_build_ = false;
  last_build_block_ = 0;

  MakeSyntheticRoom();
  built_params_.position = 0.5f;
  built_params_.size = 0.5f;
  built_params_.density = 0.5f;
  built_params_.texture = 0.5f;
  built_params_.pitch = 0.0f;
  pending_params_ = built_params_;
  BuildAll();
  return true;
}

void ConvolveMode::Process(const Controls& c, const FloatFrame* in,
                           FloatFrame* out, size_t n) {
  ctl_ = c;
  if ((c.freeze && !prev_freeze_) || c.trigger) capture_request_ = true;
  prev_freeze_ = c.freeze;
  for (size_t i = 0; i < n; ++i) {
    in_buf_[parity_][0][fill_] = in[i].l;
    in_buf_[parity_][1][fill_] = in[i].r;
    out[i].l = out_buf_[parity_][0][fill_];
    out[i].r = out_buf_[parity_][1][fill_];
    if (++fill_ >= B) {
      fill_ = 0;
      parity_ ^= 1;
      ++blocks_ready_;
    }
  }
}

void ConvolveMode::Quantize(const float* spec, int16_t* dst, float* scale) {
  float peak = 0.0f;
  for (size_t k = 0; k < kFft; ++k) {
    float a = fabsf(spec[k]);
    if (a > peak) peak = a;
  }
  if (peak < 1e-20f) {
    memset(dst, 0, kFft * sizeof(int16_t));
    *scale = 0.0f;
    return;
  }
  const float q = 32767.0f / peak;
  for (size_t k = 0; k < kFft; ++k) {
    dst[k] = static_cast<int16_t>(RoundToInt(spec[k] * q));
  }
  *scale = peak / 32767.0f;
}

void ConvolveMode::ProcessAudioBlock() {
  const int p = static_cast<int>(blocks_done_ & 1);
  const float inv_n = 1.0f / static_cast<float>(kFft);
  for (int ch = 0; ch < 2; ++ch) {
    memcpy(work_, prev_[ch], B * sizeof(float));
    memcpy(work_ + B, in_buf_[p][ch], B * sizeof(float));
    memcpy(prev_[ch], in_buf_[p][ch], B * sizeof(float));
    FftDirect(work_, spec_, kFft);
    Quantize(spec_, fdl_[ch][head_], &fdl_scale_[ch][head_]);

    memset(acc_, 0, kFft * sizeof(float));
    float* acc_re = acc_;
    float* acc_im = acc_ + B;
    for (size_t q = 0; q < active_partitions_; ++q) {
      size_t slot = (head_ + partitions_ - q) % partitions_;
      const float s = fdl_scale_[ch][slot] * ir_scale_[q];
      if (s == 0.0f) continue;
      const int16_t* x = fdl_[ch][slot];
      const int16_t* h = ir_[q];
      acc_re[0] += s * static_cast<float>(x[0]) * static_cast<float>(h[0]);
      acc_im[0] += s * static_cast<float>(x[B]) * static_cast<float>(h[B]);
      const int16_t* xi = x + B;
      const int16_t* hi = h + B;
      for (size_t k = 1; k < B; ++k) {
        const float a = x[k], b = xi[k], c = h[k], d = hi[k];
        acc_re[k] += s * (a * c - b * d);
        acc_im[k] += s * (a * d + b * c);
      }
    }
    FftInverse(acc_, work_, kFft);
    float* o = out_buf_[p][ch];
    for (size_t i = 0; i < B; ++i) o[i] = work_[B + i] * inv_n;
  }
  head_ = (head_ + 1) % partitions_;
}

void ConvolveMode::CaptureStep() {
  const float inv_n = 1.0f / static_cast<float>(kFft);
  for (int k = 0; k < 8 && capture_left_ > 0; ++k) {
    size_t i = partitions_ - capture_left_;   // 0 = oldest
    size_t slot = (capture_head_ + i) % partitions_;
    const float sl = 0.5f * fdl_scale_[0][slot];
    const float sr = 0.5f * fdl_scale_[1][slot];
    const int16_t* xl = fdl_[0][slot];
    const int16_t* xr = fdl_[1][slot];
    for (size_t j = 0; j < kFft; ++j) {
      acc_[j] = static_cast<float>(xl[j]) * sl + static_cast<float>(xr[j]) * sr;
    }
    FftInverse(acc_, work_, kFft);
    float peak = 0.0f;
    for (size_t t = 0; t < B; ++t) {
      float a = fabsf(work_[B + t]);
      if (a > peak) peak = a;
    }
    int16_t* dst = snapshot_ + i * B;
    if (peak < 1e-20f) {
      memset(dst, 0, B * sizeof(int16_t));
      snap_scale_[i] = 0.0f;
    } else {
      const float q = 32767.0f / peak;
      for (size_t t = 0; t < B; ++t) {
        dst[t] = static_cast<int16_t>(RoundToInt(work_[B + t] * q));
      }
      snap_scale_[i] = peak / 32767.0f * inv_n;
    }
    --capture_left_;
  }
  if (capture_left_ == 0) {
    ++captures_;
    force_build_ = true;
  }
}

void ConvolveMode::MakeSyntheticRoom() {
  // A small, slightly bright room: direct sound, a few early reflections,
  // then an exponentially decaying noise tail (T60 ~ 0.25 s).
  Random rng;
  rng.Seed(0xC0FFEE);
  const size_t s = partitions_ * B;
  const float k = expf(-6.9f / (0.25f * kSampleRate));
  float env = 0.5f;
  float lp = 0.0f;
  for (size_t p = 0; p < partitions_; ++p) {
    float block[B];
    float peak = 0.0f;
    for (size_t t = 0; t < B; ++t) {
      size_t i = p * B + t;
      lp += 0.6f * (rng.Bipolar() - lp);
      float x = lp * env;
      env *= k;
      if (i == 0) x += 1.0f;
      if (i == 331) x += 0.45f;
      if (i == 587) x -= 0.35f;
      if (i == 941) x += 0.25f;
      if (i >= s) x = 0.0f;
      block[t] = x;
      if (fabsf(x) > peak) peak = fabsf(x);
    }
    const float q = peak > 0.0f ? 32767.0f / peak : 0.0f;
    for (size_t t = 0; t < B; ++t) {
      snapshot_[p * B + t] = static_cast<int16_t>(RoundToInt(block[t] * q));
    }
    snap_scale_[p] = peak / 32767.0f;
  }
}

float ConvolveMode::Raw(float pos) const {
  const int32_t last = static_cast<int32_t>(partitions_ * B) - 1;
  int32_t i = static_cast<int32_t>(pos);
  float t = pos - static_cast<float>(i);
  float v[4];
  for (int k = 0; k < 4; ++k) {
    int32_t j = i - 1 + k;
    if (j < 0) j = 0;
    if (j > last) j = last;
    v[k] = snapshot_[j] * snap_scale_[j / B];
  }
  return Hermite(v[0], v[1], v[2], v[3], t);
}

void ConvolveMode::SetupGenerator() {
  const IrParams& p = pending_params_;
  const float s = static_cast<float>(partitions_ * B) - 4.0f;
  rate_ = Clamp(SemitonesToRatio(Clamp(p.pitch, -48.0f, 48.0f)), 0.25f, 4.0f);
  float size = Clamp(p.size, 0.0f, 1.0f);
  float len = 240.0f + size * size * (static_cast<float>(partitions_ * B) - 240.0f);
  if (len * rate_ > s) len = s / rate_;
  if (len < 32.0f) len = 32.0f;
  ir_length_ = static_cast<size_t>(len);
  start_ = (s - len * rate_) * (1.0f - Clamp(p.position, 0.0f, 1.0f)) + 1.0f;
  if (start_ < 1.0f) start_ = 1.0f;
  const float d = Clamp(p.density, 0.0f, 1.0f);
  reverse_ = d < 0.5f ? (0.5f - d) * 2.0f : 0.0f;
  decay_ = d > 0.5f ? expf(-(1.0f + (d - 0.5f) * 60.0f) / len) : 1.0f;
  const float t = Clamp(p.texture, 0.0f, 1.0f);
  if (t < 0.5f) {
    lowpass_ = true;
    float fc = 300.0f * exp2f(t * 12.0f);
    lp_coef_ = fc > 18000.0f ? 1.0f : 1.0f - expf(-kTwoPi * fc / kSampleRate);
    treble_ = 0.0f;
  } else {
    lowpass_ = false;
    treble_ = (t - 0.5f) * 6.0f;
    hp_coef_ = 1.0f - expf(-kTwoPi * 1000.0f / kSampleRate);
  }
}

float ConvolveMode::NextIrSample(Generator* g) const {
  const size_t n = g->n++;
  if (n >= ir_length_) return 0.0f;
  float x = Raw(start_ + static_cast<float>(n) * rate_);
  if (reverse_ > 0.0f) {
    float y = Raw(start_ + static_cast<float>(ir_length_ - 1 - n) * rate_);
    x += reverse_ * (y - x);
  }
  x *= g->env;
  g->env *= decay_;
  const size_t fade_start = ir_length_ - ir_length_ / 10;
  if (n > fade_start) {
    x *= static_cast<float>(ir_length_ - n) /
        static_cast<float>(ir_length_ - fade_start);
  }
  if (lowpass_) {
    // Two one-pole stages: 12 dB/oct above the cutoff.
    g->lp += lp_coef_ * (x - g->lp);
    g->hp_lp += lp_coef_ * (g->lp - g->hp_lp);
    x = g->hp_lp;
  } else {
    g->hp_lp += hp_coef_ * (x - g->hp_lp);
    x += treble_ * (x - g->hp_lp);
  }
  return x;
}

void ConvolveMode::StartBuild(const IrParams& p) {
  pending_params_ = p;
  SetupGenerator();
  gen_.n = 0;
  gen_.env = 1.0f;
  gen_.lp = 0.0f;
  gen_.hp_lp = 0.0f;
  energy_ = 0.0f;
  build_state_ = BUILD_ENERGY;
}

void ConvolveMode::BuildStep(int max_partitions) {
  if (build_state_ == BUILD_ENERGY) {
    for (int k = 0; k < 4096 && gen_.n < ir_length_; ++k) {
      float x = NextIrSample(&gen_);
      energy_ += x * x;
    }
    if (gen_.n >= ir_length_) {
      // Unit energy, then -6 dB: tonal input through a coloured IR builds up
      // well above the level of white noise.
      norm_ = energy_ > 1e-20f ? 0.5f / sqrtf(energy_) : 0.0f;
      gen_.n = 0;
      gen_.env = 1.0f;
      gen_.lp = 0.0f;
      gen_.hp_lp = 0.0f;
      build_partition_ = 0;
      size_t parts = (ir_length_ + B - 1) / B;
      if (parts > active_partitions_) active_partitions_ = parts;
      build_state_ = BUILD_PARTITIONS;
    }
    return;
  }
  if (build_state_ != BUILD_PARTITIONS) return;
  const size_t parts = (ir_length_ + B - 1) / B;
  for (int k = 0; k < max_partitions && build_partition_ < parts; ++k) {
    for (size_t t = 0; t < B; ++t) work_[t] = NextIrSample(&gen_) * norm_;
    memset(work_ + B, 0, B * sizeof(float));
    FftDirect(work_, spec_, kFft);
    Quantize(spec_, ir_[build_partition_], &ir_scale_[build_partition_]);
    ++build_partition_;
  }
  if (build_partition_ >= parts) {
    for (size_t q = parts; q < kMaxPartitions; ++q) ir_scale_[q] = 0.0f;
    active_partitions_ = parts;
    built_params_ = pending_params_;
    build_state_ = BUILD_IDLE;
  }
}

void ConvolveMode::BuildAll() {
  StartBuild(pending_params_);
  while (build_state_ != BUILD_IDLE) BuildStep(kMaxPartitions);
}

bool ConvolveMode::ParamsChanged(const IrParams& a, const IrParams& b) const {
  return fabsf(a.position - b.position) > 0.004f ||
      fabsf(a.size - b.size) > 0.004f ||
      fabsf(a.density - b.density) > 0.004f ||
      fabsf(a.texture - b.texture) > 0.004f ||
      fabsf(a.pitch - b.pitch) > 0.05f;
}

void ConvolveMode::Prepare() {
  if (blocks_done_ != blocks_ready_) {
    if (blocks_ready_ - blocks_done_ > 1) blocks_done_ = blocks_ready_ - 1;
    ProcessAudioBlock();
    ++blocks_done_;
  }
  if (capture_left_ > 0) {
    CaptureStep();
    return;
  }
  if (capture_request_) {
    capture_request_ = false;
    capture_head_ = head_;   // oldest slot (next to be overwritten)
    capture_left_ = partitions_;
    CaptureStep();
    return;
  }
  if (build_state_ != BUILD_IDLE) {
    BuildStep(4);
    return;
  }
  IrParams cur;
  cur.position = ctl_.position;
  cur.size = ctl_.size;
  cur.density = ctl_.density;
  cur.texture = ctl_.texture;
  cur.pitch = ctl_.pitch;
  // Debounced: at most one rebuild start per ~21 ms (4 blocks).
  if (force_build_ ||
      (ParamsChanged(cur, built_params_) &&
       blocks_done_ - last_build_block_ >= 4)) {
    force_build_ = false;
    last_build_block_ = blocks_done_;
    StartBuild(cur);
  }
}

size_t ConvolveMode::GetPersistentData(PersistentChunk* chunks) {
  static ConvolveSaveHeader header;
  header.magic = kSaveMagic;
  header.partitions = static_cast<uint32_t>(partitions_);
  header.params = built_params_;
  chunks[0].data = &header;
  chunks[0].size = sizeof(header);
  chunks[1].data = snap_scale_;
  chunks[1].size = kMaxPartitions * sizeof(float);
  chunks[2].data = snapshot_;
  chunks[2].size = partitions_ * B * sizeof(int16_t);
  return 3;
}

bool ConvolveMode::LoadPersistentData(const uint8_t* data, size_t size) {
  const size_t h = (sizeof(ConvolveSaveHeader) + 3) & ~size_t(3);
  const size_t scales = kMaxPartitions * sizeof(float);
  if (size < h + scales) return false;
  ConvolveSaveHeader header;
  memcpy(&header, data, sizeof(header));
  if (header.magic != kSaveMagic || header.partitions != partitions_) {
    return false;
  }
  if (size < h + scales + partitions_ * B * sizeof(int16_t)) return false;
  memcpy(snap_scale_, data + h, scales);
  memcpy(snapshot_, data + h + scales, partitions_ * B * sizeof(int16_t));
  pending_params_ = header.params;
  BuildAll();
  return true;
}

}  // namespace monsoon
