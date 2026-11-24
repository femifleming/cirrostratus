// The Monsoon HiFi engine.

#include "monsoon/dsp/engine.h"

#include "monsoon/dsp/modes/capture.h"
#include "monsoon/dsp/modes/chloe.h"
#include "monsoon/dsp/modes/faltung.h"
#include "monsoon/dsp/modes/quell.h"
#include "monsoon/dsp/modes/leitner.h"
#include "monsoon/dsp/modes/tantri.h"
#include "monsoon/dsp/modes/harmonizer.h"
#include "monsoon/dsp/modes/putz.h"
#include "monsoon/dsp/modes/ensemble.h"
#include "monsoon/dsp/modes/barberpole.h"
#include "monsoon/dsp/modes/specdelay.h"
#include "monsoon/dsp/modes/choir.h"
#include "monsoon/dsp/modes/folder.h"
#include "monsoon/dsp/modes/hall.h"
#include "monsoon/dsp/modes/bloom.h"
#include "monsoon/dsp/modes/gamelan.h"
#include "monsoon/dsp/modes/mirror.h"
#include "monsoon/dsp/modes/polish.h"
#include "monsoon/dsp/modes/convolve.h"
#include "monsoon/dsp/modes/cross.h"
#include "monsoon/dsp/modes/difference.h"
#include "monsoon/dsp/modes/gendy.h"
#include "monsoon/dsp/modes/grains.h"
#include "monsoon/dsp/modes/plates.h"
#include "monsoon/dsp/modes/resonator.h"
#include "monsoon/dsp/modes/shimmer.h"
#include "monsoon/dsp/modes/spectral.h"
#include "monsoon/dsp/modes/spreek.h"
#include "monsoon/dsp/modes/stretch.h"
#include "monsoon/dsp/modes/tape.h"
#include "monsoon/dsp/stft.h"

namespace monsoon {

static Fft shared_fft;
static bool shared_fft_ready = false;

Fft& SharedFft() {
  if (!shared_fft_ready) {
    shared_fft.Init();
    shared_fft_ready = true;
  }
  return shared_fft;
}

// One instance of every mode. Their big buffers live in the arena; the
// objects themselves are small.
static GrainsMode grains_mode;
static SpectralMode spectral_mode;
static ConvolveMode convolve_mode;
static ResonatorMode resonator_mode;
static DifferenceMode difference_mode;
static CrossMode cross_mode;
static StretchMode stretch_mode;
static SpreekMode spreek_mode;
static CaptureMode capture_mode;
static GendyMode gendy_mode;
static ShimmerMode shimmer_mode;
static TapeMode tape_mode;
static PlatesMode plates_mode;
static ChloeMode chloe_mode;
static FaltungMode faltung_mode;
static QuellMode quell_mode;
static LeitnerMode leitner_mode;
static TantriMode tantri_mode;
static HarmonizerMode harmonizer_mode;
static PutzMode putz_mode;
static EnsembleMode ensemble_mode;
static BarberpoleMode barberpole_mode;
static SpecDelayMode specdelay_mode;
static ChoirMode choir_mode;
static FolderMode folder_mode;
static HallMode hall_mode;
static BloomMode bloom_mode;
static GamelanMode gamelan_mode;
static MirrorMode mirror_mode;
static PolishMode polish_mode;

static Mode* const kModes[MODE_LAST] = {
  &grains_mode, &spectral_mode, &convolve_mode, &resonator_mode,
  &difference_mode, &cross_mode, &stretch_mode, &spreek_mode,
  &capture_mode, &gendy_mode, &shimmer_mode, &tape_mode, &plates_mode,
  &chloe_mode,
  &faltung_mode, &quell_mode, &leitner_mode, &tantri_mode, &harmonizer_mode,
  &putz_mode, &ensemble_mode, &barberpole_mode, &specdelay_mode, &choir_mode, &folder_mode,
  &hall_mode, &bloom_mode, &gamelan_mode, &mirror_mode, &polish_mode
};

static const size_t kFadeSamples = 480;  // 10 ms

// Header stored at the start of every save slot.
struct SlotHeader {
  uint32_t magic;
  uint16_t version;
  uint8_t mode;
  uint8_t option;
  uint32_t payload_size;
};
static const uint32_t kSlotMagic = 0x4E534E4D;  // "MNSN"
static SlotHeader slot_header;

Mode* Engine::mode_instance(ModeId id) { return kModes[id]; }

const char* Engine::mode_name(ModeId id) { return kModes[id]->name(); }

uint8_t Engine::num_options(ModeId id) { return kModes[id]->num_options(); }

bool Engine::Init(void* main_mem, size_t main_size,
                  void* ccm_mem, size_t ccm_size) {
  SharedFft();
  size_t reverb_bytes = Fdn::MemoryBytes() + 64;
  if (ccm_size < reverb_bytes) return false;
  reverb_arena_.Init(ccm_mem, reverb_bytes, NULL, 0);
  arena_.Init(main_mem, main_size,
              static_cast<uint8_t*>(ccm_mem) + reverb_bytes,
              ccm_size - reverb_bytes);
  if (!reverb_.Init(&reverb_arena_)) return false;
  tamer_.Init();
  spread_.Init();
  limiter_.Init();
  for (int i = 0; i < 2; ++i) {
    fb_hp_[i].Init();
    fb_hp_[i].set_f(25.0f / kSampleRate);
    out_dc_[i].Init(0.9997f);
  }
  dither_.Seed(0xBEEF);
  envelope_.Init(0.004f, 0.18f);
  memset(&env_routing_, 0, sizeof(env_routing_));
  memset(feedback_signal_, 0, sizeof(feedback_signal_));
  blend_.Init(0.5f);
  spread_amt_.Init(0.5f);
  feedback_.Init(0.0f);
  reverb_amt_.Init(0.0f);
  silence_ = false;
  pending_load_data_ = NULL;
  pending_load_size_ = 0;

  // Bring up the first mode synchronously.
  active_mode_ = MODE_GRAINS;
  active_option_ = 0;
  requested_mode_ = MODE_GRAINS;
  requested_option_ = 0;
  arena_.Reset();
  current_ = kModes[active_mode_];
  current_->set_init_option(0);
  init_failed_ = !current_->Init(&arena_);
  state_ = STATE_FADING_IN;
  fade_ = 0.0f;
  return !init_failed_;
}

void Engine::RequestMode(ModeId mode, uint8_t option) {
  if (mode >= MODE_LAST) return;
  if (option >= kModes[mode]->num_options()) option = 0;
  requested_option_ = option;
  if (mode != active_mode_) {
    requested_mode_ = mode;
    if (state_ == STATE_RUNNING || state_ == STATE_FADING_IN) {
      state_ = STATE_FADING_OUT;
    }
  } else {
    requested_mode_ = mode;
    if (option != active_option_ && kModes[mode]->option_requires_init() &&
        (state_ == STATE_RUNNING || state_ == STATE_FADING_IN)) {
      state_ = STATE_FADING_OUT;
    }
  }
}

void Engine::RequestLoad(const uint8_t* data, size_t size) {
  if (!data || size < sizeof(SlotHeader)) return;
  const SlotHeader* h = reinterpret_cast<const SlotHeader*>(data);
  if (h->magic != kSlotMagic || h->mode >= MODE_LAST) return;
  if (h->payload_size + sizeof(SlotHeader) > size) return;
  pending_load_data_ = data;
  pending_load_size_ = size;
  requested_mode_ = static_cast<ModeId>(h->mode);
  requested_option_ = h->option;
  state_ = STATE_FADING_OUT;
}

size_t Engine::GetPersistentData(PersistentChunk* chunks) {
  PersistentChunk mode_chunks[kMaxPersistentChunks];
  size_t n = current_->GetPersistentData(mode_chunks);
  if (n == 0) return 0;
  size_t payload = 0;
  for (size_t i = 0; i < n; ++i) payload += (mode_chunks[i].size + 3) & ~3u;
  slot_header.magic = kSlotMagic;
  slot_header.version = 1;
  slot_header.mode = static_cast<uint8_t>(active_mode_);
  slot_header.option = active_option_;
  slot_header.payload_size = static_cast<uint32_t>(payload);
  chunks[0].data = &slot_header;
  chunks[0].size = sizeof(slot_header);
  for (size_t i = 0; i < n; ++i) chunks[i + 1] = mode_chunks[i];
  return n + 1;
}

void Engine::SetMorphSource(const uint8_t* data, size_t size) {
  if (!data) {
    current_->SetMorphSource(NULL, 0);
    return;
  }
  if (size < sizeof(SlotHeader)) return;
  const SlotHeader* h = reinterpret_cast<const SlotHeader*>(data);
  if (h->magic != kSlotMagic || h->mode != active_mode_) return;
  current_->SetMorphSource(data + sizeof(SlotHeader), h->payload_size);
}

bool Engine::supports_morph() { return current_->supports_morph(); }

void Engine::Prepare() {
  if (state_ == STATE_SWITCH_PENDING) {
    active_mode_ = requested_mode_;
    active_option_ = requested_option_;
    arena_.Reset();
    current_ = kModes[active_mode_];
    current_->set_init_option(active_option_);
    init_failed_ = !current_->Init(&arena_);
    if (pending_load_data_) {
      const SlotHeader* h =
          reinterpret_cast<const SlotHeader*>(pending_load_data_);
      if (!init_failed_ && h->mode == active_mode_) {
        current_->LoadPersistentData(pending_load_data_ + sizeof(SlotHeader),
                                     h->payload_size);
      }
      pending_load_data_ = NULL;
    }
    memset(feedback_signal_, 0, sizeof(feedback_signal_));
    fade_ = 0.0f;
    state_ = STATE_FADING_IN;
    return;
  }
  if (state_ == STATE_RUNNING || state_ == STATE_FADING_IN) {
    if (requested_option_ != active_option_ && requested_mode_ == active_mode_) {
      active_option_ = requested_option_;
    }
    current_->Prepare();
  }
}

void Engine::ApplyEnvelope(const Controls& raw, Controls* c) {
  *c = raw;
  c->option = active_option_;
  float env = Clamp(envelope_.value() * 2.5f, 0.0f, 1.0f);
  float* targets[ENV_TARGET_LAST] = {
    &c->blend, &c->spread, &c->feedback, &c->reverb
  };
  for (int i = 0; i < ENV_TARGET_LAST; ++i) {
    if (env_routing_.amount[i] != 0.0f) {
      *targets[i] = Clamp(*targets[i] + env * env_routing_.amount[i],
                          0.0f, 1.0f);
    }
  }
}

void Engine::ProcessFloat(const Controls& raw, const FloatFrame* in,
                          FloatFrame* out, size_t n) {
  // Input envelope (drives the optional slider modulation).
  for (size_t i = 0; i < n; ++i) {
    envelope_.Process(0.5f * (in[i].l + in[i].r));
  }
  Controls c;
  ApplyEnvelope(raw, &c);

  // Feedback goes into the mode together with the input.
  for (size_t i = 0; i < n; ++i) {
    dry_[i] = in[i];
    mode_in_[i].l = in[i].l + feedback_signal_[i].l;
    mode_in_[i].r = in[i].r + feedback_signal_[i].r;
  }

  bool run_mode = !init_failed_ &&
      (state_ == STATE_RUNNING || state_ == STATE_FADING_IN ||
       state_ == STATE_FADING_OUT);
  if (run_mode) {
    current_->Process(c, mode_in_, wet_, n);
  } else {
    memset(wet_, 0, sizeof(FloatFrame) * n);
  }

  // Fades for mode switching.
  const float fade_inc = 1.0f / kFadeSamples;
  for (size_t i = 0; i < n; ++i) {
    if (state_ == STATE_FADING_OUT) {
      fade_ -= fade_inc;
      if (fade_ <= 0.0f) {
        fade_ = 0.0f;
        state_ = STATE_SWITCH_PENDING;
      }
    } else if (state_ == STATE_FADING_IN) {
      fade_ += fade_inc;
      if (fade_ >= 1.0f) {
        fade_ = 1.0f;
        state_ = STATE_RUNNING;
      }
    } else if (state_ != STATE_RUNNING) {
      fade_ = 0.0f;
    }
    float g = fade_;
    wet_[i].l *= g;
    wet_[i].r *= g;
  }

  // Equal-power blend gains, computed at block rate and ramped.
  const float b0 = blend_.value();
  const float b1 = c.blend;
  float wet_gain = FastSin(b0 * 0.25f), dry_gain = FastCos(b0 * 0.25f);
  const float inv_n = 1.0f / static_cast<float>(n);
  const float wet_inc = (FastSin(b1 * 0.25f) - wet_gain) * inv_n;
  const float dry_inc = (FastCos(b1 * 0.25f) - dry_gain) * inv_n;
  blend_.Init(b1);
  spread_amt_.Start(c.spread, n);
  feedback_.Start(c.feedback, n);
  for (size_t i = 0; i < n; ++i) {
    float l = wet_[i].l;
    float r = wet_[i].r;
    // A runaway mode must never produce NaN/inf downstream.
    if (!(fabsf(l) < 64.0f)) l = 0.0f;
    if (!(fabsf(r) < 64.0f)) r = 0.0f;

    // Feedback path: HP, Quell-style resonance taming, saturation.
    float fb = feedback_.Next();
    float fb_gain = fb * fb * (3.0f - 2.0f * fb) * 1.02f;
    float hl = fb_hp_[0].Hp(l);
    float hr = fb_hp_[1].Hp(r);
    float cut = tamer_.Process(0.5f * (hl + hr), Clamp(fb * 1.6f, 0.0f, 1.0f));
    feedback_signal_[i].l = SoftClip((hl - cut) * fb_gain);
    feedback_signal_[i].r = SoftClip((hr - cut) * fb_gain);

    // Remove any DC the mode produced, then stereo spread on the wet signal.
    l = out_dc_[0].Process(l);
    r = out_dc_[1].Process(r);
    spread_.Process(spread_amt_.Next(), &l, &r);

    wet_gain += wet_inc;
    dry_gain += dry_inc;
    out[i].l = dry_[i].l * dry_gain + l * wet_gain;
    out[i].r = dry_[i].r * dry_gain + r * wet_gain;
  }
  spread_amt_.Finish();
  feedback_.Finish();

  reverb_.Process(c.reverb, false, out, n);

  for (size_t i = 0; i < n; ++i) {
    float l = out[i].l;
    float r = out[i].r;
    limiter_.Process(&l, &r);
    out[i].l = l;
    out[i].r = r;
  }
  // While flash is written the whole output is muted, so the codec's DMA
  // loop (which keeps cycling the last buffer) only repeats silence.
  if (silence_) memset(out, 0, sizeof(FloatFrame) * n);
}

void Engine::Process(const Controls& raw, const ShortFrame* in,
                     ShortFrame* out, size_t n) {
  FloatFrame fin[kBlockSize];
  FloatFrame fout[kBlockSize];
  const float k = 1.0f / 32768.0f;
  for (size_t i = 0; i < n; ++i) {
    fin[i].l = in[i].l * k;
    fin[i].r = in[i].r * k;
  }
  ProcessFloat(raw, fin, fout, n);
  // TPDF dither at 1 LSB: the difference of two uniform 16-bit values
  // taken from one 32-bit random number per channel.
  const float kDither = 1.0f / 65536.0f;
  for (size_t i = 0; i < n; ++i) {
    uint32_t ra = dither_.Next(), rb = dither_.Next();
    float dl = (static_cast<int32_t>(ra & 0xffff) - static_cast<int32_t>(ra >> 16)) * kDither;
    float dr = (static_cast<int32_t>(rb & 0xffff) - static_cast<int32_t>(rb >> 16)) * kDither;
    float vl = fout[i].l * 32767.0f + dl;
    float vr = fout[i].r * 32767.0f + dr;
    int32_t l = static_cast<int32_t>(vl + (vl >= 0.0f ? 0.5f : -0.5f));
    int32_t r = static_cast<int32_t>(vr + (vr >= 0.0f ? 0.5f : -0.5f));
    if (l > 32767) l = 32767;
    if (l < -32767) l = -32767;
    if (r > 32767) r = 32767;
    if (r < -32767) r = -32767;
    out[i].l = static_cast<int16_t>(l);
    out[i].r = static_cast<int16_t>(r);
  }
}

}  // namespace monsoon
