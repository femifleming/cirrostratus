// Mode 12: TAPE (see tape.h).

#include "monsoon/dsp/modes/tape.h"

namespace monsoon {

static const size_t kTapeBytes = 118 * 1024;  // fits a save slot

// Head combinations selected by POSITION (9 zones). Output weights per
// head, and which heads feed the repeats.
struct HeadZone {
  float out[TapeMode::kHeads];
  float fb[TapeMode::kHeads];
};
static const int kNumZones = 9;
static const HeadZone kZones[kNumZones] = {
  { { 1, 0, 0 }, { 1, 0, 0 } },              // 1
  { { 0, 1, 0 }, { 0, 1, 0 } },              // 2
  { { 0, 0, 1 }, { 0, 0, 1 } },              // 3
  { { 0.8f, 0.8f, 0 }, { 0, 1, 0 } },        // 1+2
  { { 0, 0.8f, 0.8f }, { 0, 0, 1 } },        // 2+3
  { { 0.8f, 0, 0.8f }, { 0, 0, 1 } },        // 1+3
  { { 0.7f, 0.7f, 0.7f }, { 0, 0, 1 } },     // 1+2+3
  { { 0.7f, 0.7f, 0.7f }, { 0.34f, 0.33f, 0.33f } },  // all heads feed back
  { { 0.35f, 0.6f, 1.0f }, { 0.34f, 0.33f, 0.33f } }, // swell
};

bool TapeMode::Init(Arena* arena) {
  channels_ = init_option_ == 1 ? 1 : 2;
  if (!tape_.Init(arena, kTapeBytes, channels_)) return false;
  staged_ = 0;
  acc_ = 0.0f;
  // Heads at 1, 2, 3 x spacing; keep room for wow and the interpolator.
  spacing_ = static_cast<float>(tape_.size() - 512) / 3.06f;
  memset(hist_, 0, sizeof(hist_));
  for (int ch = 0; ch < 2; ++ch) {
    aa_[ch][0].Init();
    aa_[ch][1].Init();
    aa_[ch][2].Init();
    bump_[ch].Init();
    bump_[ch].set(95.0f / kSampleRate, 1.2f);
    loss_[ch].Init();
    loss_[ch].set_f(7000.0f / kSampleRate);
    fb_[ch] = 0.0f;
  }
  for (int k = 0; k < kHeads; ++k) {
    head_gain_[k] = kZones[0].out[k];
    fb_gain_[k] = kZones[0].fb[k];
  }
  speed_ = 1.0f;
  speed_target_ = 1.0f;
  stop_mult_ = 1.0f;
  stopped_ = false;
  input_gain_ = 1.0f;
  wow_phase_ = 0.0f;
  flutter_phase_ = 0.0f;
  flutter_noise_ = 0.0f;
  rng_.Seed(0x7A9E);
  frozen_ = false;
  memset(stage_, 0, sizeof(stage_));
  return true;
}

void TapeMode::WriteTapeSample(float l, float r) {
  stage_[staged_].l = l;
  stage_[staged_].r = channels_ == 1 ? l : r;
  if (++staged_ == kBlockSize) {
    tape_.WriteBlock(stage_);
    staged_ = 0;
  }
}

inline float TapeMode::ReadHead(int channel, float distance) const {
  // Newest tape sample is at write_head + staged - 1; acc_ is how far the
  // tape has moved towards the next one.
  float now = static_cast<float>(tape_.write_head() + staged_) - 1.0f + acc_;
  return tape_.Read(channel, now - distance);
}

void TapeMode::Process(const Controls& c, const FloatFrame* in,
                       FloatFrame* out, size_t n) {
  // --- Block-rate parameters.
  if (c.trigger) stopped_ = !stopped_;
  frozen_ = c.freeze;

  float size = Clamp(c.size, 0.0f, 1.0f);
  float base = size < 0.5f ? exp2f((size - 0.5f) * 4.0f)
                           : exp2f((size - 0.5f) * 2.0f);
  float target = Clamp(base * SemitonesToRatio(c.pitch), 0.0625f, 4.0f);
  const float speed_start = speed_target_;
  speed_target_ += 0.15f * (target - speed_target_);
  const float speed_inc = (speed_target_ - speed_start) / static_cast<float>(n);

  // Anti-alias the input for the tape's sample rate (speed x 48 kHz).
  // 6-pole Butterworth at 0.42 x the tape's sample rate.
  float aa = 0.42f * Clamp(speed_, 0.001f, 1.0f);
  for (int ch = 0; ch < 2; ++ch) {
    aa_[ch][0].set(aa, 0.5176f);
    aa_[ch][1].set(aa, 0.7071f);
    aa_[ch][2].set(aa, 1.9319f);
  }

  int zone = static_cast<int>(Clamp(c.position, 0.0f, 0.999f) * kNumZones);
  const HeadZone& z = kZones[zone];

  const float texture = Clamp(c.texture, 0.0f, 1.0f);
  const float drive = 1.0f + 2.5f * texture;
  const float inv_drive = 1.0f / drive;
  const float wow_depth = texture * 0.004f;
  const float flutter_depth = texture * 0.0012f;
  const float bump = texture * 0.6f;
  const float feedback = frozen_ ? 1.0f : Clamp(c.density, 0.0f, 1.0f) * 1.1f;
  const float in_target = frozen_ ? 0.0f : 1.0f;

  float spd = speed_start;
  for (size_t i = 0; i < n; ++i) {
    // Tape transport: stop / start ramps on top of the selected speed.
    spd += speed_inc;
    if (stopped_) {
      stop_mult_ *= 0.99985f;
      if (stop_mult_ < 0.0005f) stop_mult_ = 0.0f;
    } else {
      stop_mult_ += 0.0003f * (1.0f - stop_mult_) + (stop_mult_ < 0.01f ? 0.0002f : 0.0f);
      if (stop_mult_ > 1.0f) stop_mult_ = 1.0f;
    }
    speed_ = spd * stop_mult_;
    input_gain_ += 0.002f * (in_target - input_gain_);
    for (int k = 0; k < kHeads; ++k) {
      head_gain_[k] += 0.002f * (z.out[k] - head_gain_[k]);
      float fb_target = frozen_ ? (k == 2 ? 1.0f : 0.0f) : z.fb[k];
      fb_gain_[k] += 0.002f * (fb_target - fb_gain_[k]);
    }

    // Wow (slow) and flutter (fast, jittery) wobble the head distances.
    wow_phase_ += 0.55f / kSampleRate;
    if (wow_phase_ >= 1.0f) wow_phase_ -= 1.0f;
    flutter_phase_ += 7.3f / kSampleRate;
    if (flutter_phase_ >= 1.0f) flutter_phase_ -= 1.0f;
    flutter_noise_ += 0.001f * (rng_.Bipolar() - flutter_noise_);
    const float mod = 1.0f + wow_depth * FastSin(wow_phase_) +
        flutter_depth * (FastSin(flutter_phase_) + 2.0f * flutter_noise_);

    // --- Playback heads.
    float head[2][kHeads];
    for (int k = 0; k < kHeads; ++k) {
      float d = spacing_ * (k + 1) * mod;
      head[0][k] = ReadHead(0, d);
      head[1][k] = channels_ == 2 ? ReadHead(1, d) : head[0][k];
    }
    float o[2], fbs[2];
    for (int ch = 0; ch < 2; ++ch) {
      float sum = 0.0f, f = 0.0f;
      for (int k = 0; k < kHeads; ++k) {
        float h = head[ch][k];
        if (channels_ == 1) {
          // Mono tape: spread the heads (1 left, 2 centre, 3 right).
          static const float kPan[2][kHeads] = { { 0.85f, 0.7f, 0.45f },
                                                 { 0.45f, 0.7f, 0.85f } };
          sum += h * head_gain_[k] * kPan[ch][k] * 1.2f;
        } else {
          sum += h * head_gain_[k];
        }
        f += head[ch][k] * fb_gain_[k];
      }
      sum += bump * bump_[ch].BpNormalized(sum);
      o[ch] = sum;
      fbs[ch] = f;
    }
    out[i].l = o[0];
    out[i].r = o[1];

    // --- Record head: input + repeats, saturated and resampled to tape.
    for (int ch = 0; ch < 2; ++ch) {
      float src = ch == 0 ? in[i].l : in[i].r;
      if (channels_ == 1) src = 0.5f * (in[i].l + in[i].r);
      float repeat = frozen_ ? fbs[ch] : loss_[ch].Lp(fbs[ch]);
      float w = src * input_gain_ + repeat * feedback;
      if (!frozen_) w = SoftClip(w * drive) * inv_drive;
      w = aa_[ch][2].Lp(aa_[ch][1].Lp(aa_[ch][0].Lp(w)));
      float* h = hist_[ch];
      h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = w;
    }

    acc_ += speed_;
    int guard = 0;
    while (acc_ >= 1.0f && guard++ < 5) {
      acc_ -= 1.0f;
      // This tape sample sits (acc_ / speed) samples before "now";
      // interpolate inside [h1, h2] (one sample of latency).
      float t = 1.0f - acc_ / (speed_ + 1e-9f);
      t = Clamp(t, 0.0f, 1.0f);
      float l = Hermite(hist_[0][0], hist_[0][1], hist_[0][2], hist_[0][3], t);
      float r = Hermite(hist_[1][0], hist_[1][1], hist_[1][2], hist_[1][3], t);
      WriteTapeSample(l, r);
    }
    if (acc_ >= 1.0f) acc_ = 0.999f;
  }
}

}  // namespace monsoon
