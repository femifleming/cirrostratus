// Cirrostratus 1.1: the output section (hard stereo, mono bass), the
// per-mode BAL/FBK/RVB knobs on every mode, the envelope routing to them,
// and compatibility with v1.0 settings and save slots.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "host/harness.h"
#include "monsoon/settings_data.h"

using namespace host;
using namespace monsoon;

namespace {

// Fingerprints taken with the v1.0 firmware code (commit 2c6a7f9): the
// bytes a fixed recording saves into a slot, and the output energy and two
// random projections of every mode's output for fixed music at the v1.0
// default knobs (spread noon, feedback 0, reverb 0). Taken with the same
// compiler flags as this test binary (ASan/UBSan change float rounding in
// places, e.g. CAPTURE's table build).
struct V10Print {
  const char* mode;
  size_t slot_size;
  uint32_t slot_crc;
  double energy_l, energy_r, proj_l, proj_r;
};
const V10Print kV10[] = {
  { "grains", 120798, 0xc1127646u, 433.051858, 375.070922, -10.2249065, 19.3220353 },
  { "spectral", 16396, 0xb4b1aa76u, 355.421397, 258.436441, 10.2265751, -10.6315267 },
  { "convolve", 13416, 0x1a0b003bu, 2587.17193, 1807.32843, -20.1384871, 13.3925218 },
  { "resonator", 0, 0x00000000u, 378.559752, 216.152097, 21.3374112, -10.5457194 },
  { "difference", 0, 0x00000000u, 618.757244, 428.42158, 14.6814149, 1.31646298 },
  { "cross", 0, 0x00000000u, 411.663324, 411.663324, -0.429420256, 3.15993274 },
  { "stretch", 58528, 0xf90fa469u, 297.773781, 204.684962, -8.740287, -2.96767628 },
  { "spreek", 0, 0x00000000u, 527.31568, 527.31568, -27.5713803, -11.409947 },
  { "capture", 65300, 0xd2138b16u, 2555.11233, 2552.96319, 9.63755491, -0.653770903 },
  { "gendy", 660, 0x38c46dfbu, 223.531687, 408.266253, 9.78405549, 2.89588632 },
  { "shimmer", 0, 0x00000000u, 923.124589, 935.700494, 7.69411137, 32.9224536 },
  { "tape", 120798, 0xe94c70f9u, 742.402611, 541.73721, 2.02642084, 10.3506675 },
  { "plates", 0, 0x00000000u, 878.324639, 685.227361, -15.93464, 21.2875314 },
  { "chloe", 0, 0x00000000u, 4783.22377, 3669.50477, 29.4878188, -7.66484444 },
  { "faltung", 0, 0x00000000u, 430.454658, 287.233695, 2.93816707, -3.24959733 },
  { "quell", 0, 0x00000000u, 331.363469, 241.705087, -6.82326395, -2.19357631 },
  { "leitner", 0, 0x00000000u, 216.994095, 169.761344, -1.37857527, 7.0440985 },
  { "tantri", 0, 0x00000000u, 989.393252, 616.534765, 6.50461213, -40.2555979 },
  { "harmonizer", 0, 0x00000000u, 1553.00401, 1150.75498, 22.1349704, -29.2032372 },
  { "putz", 0, 0x00000000u, 421.997613, 297.164518, -0.677423282, -8.71361087 },
  { "ensemble", 0, 0x00000000u, 1252.02229, 922.44726, -8.06989477, -18.0107414 },
  { "barberpole", 0, 0x00000000u, 1026.40148, 753.36824, -18.5265439, -7.53447271 },
  { "specdelay", 0, 0x00000000u, 966.880306, 735.647029, 9.58835349, 19.8873509 },
  { "choir", 0, 0x00000000u, 243.824492, 281.565692, -5.66476906, 4.64951968 },
  { "folder", 0, 0x00000000u, 13999.4298, 13482.9885, 35.22417, -10.2169855 },
  { "hall", 0, 0x00000000u, 155.743757, 159.397956, 4.59235018, 8.0623206 },
  { "bloom", 0, 0x00000000u, 280.184378, 211.103877, 14.6267419, 4.48399293 },
  { "gamelan", 0, 0x00000000u, 236.106712, 235.760343, 1.62618105, -7.36312922 },
  { "mirror", 0, 0x00000000u, 959.807727, 959.807727, -13.5933346, -8.9518154 },
  { "polish", 0, 0x00000000u, 851.29636, 857.687646, -4.43971731, 7.64431064 },
};

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
  c = ~c;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

// A stereo test signal: one sine on the left, another (or none) on the right.
std::vector<FloatFrame> TwoSines(float hz_l, float amp_l, float hz_r,
                                 float amp_r, float seconds) {
  std::vector<FloatFrame> v(size_t(seconds * 48000) & ~size_t(31));
  for (size_t i = 0; i < v.size(); ++i) {
    v[i].l = amp_l * std::sin(2.0 * M_PI * hz_l * i / 48000.0);
    v[i].r = amp_r * std::sin(2.0 * M_PI * hz_r * i / 48000.0);
  }
  return v;
}

std::vector<FloatFrame> RunMonoBass(const std::vector<FloatFrame>& in,
                                    bool on) {
  MonoBass mb;
  mb.Init(60.0f);
  mb.set_on(on, true);
  std::vector<FloatFrame> out = in;
  for (size_t i = 0; i < out.size(); i += kBlockSize) {
    mb.Process(&out[i], kBlockSize);
  }
  return out;
}

const size_t kFrom = 48000, kTo = 96000;  // measure the second second

// The engine with POLISH frozen (POLISH's bypass is a plain wire), so what
// is measured is the output section.
std::vector<FloatFrame> RunWire(const std::vector<FloatFrame>& in, bool mono_bass,
                                float blend = 1.0f) {
  Module m;
  m.engine().set_mono_bass(mono_bass, true);
  m.SetMode(MODE_POLISH);
  return m.Render(in, [&](size_t) {
    Controls c = DefaultControls();
    c.freeze = true;
    c.blend = blend;
    return c;
  });
}

}  // namespace

// ---------------------------------------------------------------------------
// Mono bass (LR4 crossover at 60 Hz).

MONSOON_TEST(mono_bass_sums_lows_keeps_highs_flat_sum) {
  // Below 60 Hz: a left-only tone comes out the same on both sides.
  for (float hz : { 20.0f, 30.0f }) {
    auto out = RunMonoBass(TwoSines(hz, 0.5f, 0, 0, 2.0f), true);
    std::vector<FloatFrame> diff(out.size()), sum(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
      diff[i].l = out[i].l - out[i].r;
      sum[i].l = 0.5f * (out[i].l + out[i].r);
    }
    float d = ToneAmplitude(diff, hz, kFrom, kTo), s = ToneAmplitude(sum, hz, kFrom, kTo);
    std::printf("    left-only %2.0f Hz: L-R %.1f dB below (L+R)/2\n", hz, Db(s) - Db(d));
    // LR4 at 60 Hz: the side is down 2|HP4|: ~-32 dB at 20 Hz, ~-19 dB at 30 Hz.
    CHECK(Db(s) - Db(d) > (hz < 25.0f ? 30.0f : 18.0f), "bass not mono at %.0f Hz", hz);
    CHECK(std::fabs(Db(s) - Db(0.25f)) < 0.5f, "summed level wrong at %.0f Hz", hz);
  }
  // Above: a left-only tone stays left-only, at full level.
  for (float hz : { 250.0f, 1000.0f, 8000.0f }) {
    auto out = RunMonoBass(TwoSines(hz, 0.5f, 0, 0, 2.0f), true);
    float l = ToneAmplitude(out, hz, kFrom, kTo, true);
    float r = ToneAmplitude(out, hz, kFrom, kTo, false);
    std::printf("    left-only %4.0f Hz: L %+.2f dB, R %.1f dB\n", hz, Db(l / 0.5f), Db(r / 0.5f));
    CHECK(std::fabs(Db(l / 0.5f)) < 0.1f, "highs altered at %.0f Hz", hz);
    CHECK(Db(r / 0.5f) < (hz < 500.0f ? -40.0f : -80.0f), "leak to R at %.0f Hz", hz);
  }
  // The crossover's sum is magnitude-flat: centred material keeps its level
  // at every frequency (only the phase turns, as in any LR4 crossover).
  float worst = 0.0f;
  for (float hz : { 15.0f, 30.0f, 45.0f, 60.0f, 80.0f, 120.0f, 500.0f, 5000.0f }) {
    auto out = RunMonoBass(TwoSines(hz, 0.5f, hz, 0.5f, 2.0f), true);
    float g = Db(ToneAmplitude(out, hz, kFrom, kTo) / 0.5f);
    float gr = Db(ToneAmplitude(out, hz, kFrom, kTo, false) / 0.5f);
    worst = std::fmax(worst, std::fmax(std::fabs(g), std::fabs(gr)));
  }
  std::printf("    centred tones 15 Hz..5 kHz: largest level change %.3f dB\n", worst);
  CHECK(worst < 0.05f, "sum not flat: %.3f dB", worst);
  return true;
}

MONSOON_TEST(mono_bass_off_is_bit_transparent_and_toggles_without_clicks) {
  auto in = Music(2.0f, 3);
  auto off = RunMonoBass(in, false);
  CHECK(std::memcmp(off.data(), in.data(), in.size() * sizeof(FloatFrame)) == 0,
        "off must be bit-transparent");
  // Toggle back and forth on a bassy stereo signal: the crossfade must not
  // click (largest sample step no worse than the signal's own).
  auto sig = TwoSines(41.0f, 0.5f, 47.0f, 0.5f, 2.0f);
  MonoBass mb;
  mb.Init(60.0f);
  float own = 0.0f, worst = 0.0f;
  FloatFrame prev = sig[0];
  for (size_t i = 0; i < sig.size(); i += kBlockSize) {
    if (i % 12000 == 0) mb.set_on(!mb.on());
    for (size_t k = 1; k < kBlockSize; ++k) {
      own = std::fmax(own, std::fabs(sig[i + k].l - sig[i + k - 1].l));
    }
    mb.Process(&sig[i], kBlockSize);
    for (size_t k = 0; k < kBlockSize; ++k) {
      worst = std::fmax(worst, std::fabs(sig[i + k].l - prev.l));
      prev = sig[i + k];
    }
  }
  std::printf("    largest step while toggling %.4f (signal's own %.4f)\n", worst, own);
  CHECK(worst < own * 1.5f, "toggle clicks");
  return true;
}

MONSOON_TEST(output_is_hard_stereo_with_mono_bass_on_the_wet_bus) {
  // Through the engine: left-only 1 kHz + left-only 20 Hz.
  auto in = TwoSines(1000.0f, 0.4f, 0, 0, 2.0f);
  auto bass = TwoSines(20.0f, 0.3f, 0, 0, 2.0f);
  for (size_t i = 0; i < in.size(); ++i) in[i].l += bass[i].l;
  for (int on = 0; on < 2; ++on) {
    auto out = RunWire(in, on != 0);
    float hl = ToneAmplitude(out, 1000.0f, kFrom, kTo, true);
    float hr = ToneAmplitude(out, 1000.0f, kFrom, kTo, false);
    float bl = ToneAmplitude(out, 20.0f, kFrom, kTo, true);
    float br = ToneAmplitude(out, 20.0f, kFrom, kTo, false);
    std::printf("    mono bass %s: 1 kHz L %.1f / R %.1f dB, 20 Hz L %.1f / R %.1f dB\n",
                on ? "on " : "off", Db(hl), Db(hr), Db(bl), Db(br));
    CHECK(Db(hr) - Db(hl) < -80.0f, "a left-only 1 kHz tone must stay left");
    CHECK(std::fabs(Db(hl / 0.4f)) < 0.1f, "1 kHz level changed");
    if (on) {
      CHECK(std::fabs(Db(bl) - Db(br)) < 0.3f, "20 Hz should be on both sides");
    } else {
      CHECK(Db(br) - Db(bl) < -80.0f, "mono bass off: 20 Hz stays left");
    }
  }
  // Fully dry stays bit-transparent with mono bass on.
  auto music = Music(1.0f, 9);
  for (auto& f : music) { f.l *= 0.5f; f.r *= 0.5f; }  // below the limiter's knee
  auto dry = RunWire(music, true, 0.0f);
  bool same = true;
  for (size_t i = 4800; i < music.size(); ++i) {  // after WET's first ramp
    if (dry[i].l != music[i].l || dry[i].r != music[i].r) { same = false; break; }
  }
  CHECK(same, "fully dry must be bit-transparent");
  return true;
}

// ---------------------------------------------------------------------------
// BAL / FBK / RVB on every mode.

MONSOON_TEST(every_mode_bal_fbk_rvb_sweeps_and_random_cv_are_safe) {
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    const char* name = Engine::mode_name(ModeId(mode));
    Module m;
    CHECK(m.SetMode(ModeId(mode)), "%s init", name);
    auto ref_in = Music(2.0f, 21 + mode);
    auto ref = m.Render(ref_in, [](size_t b) {
      Controls c = DefaultControls();
      c.trigger = (b % 500) == 0;
      c.gate = (b % 500) < 200;
      return c;
    });
    float ref_rms = Measure(ref, ref.size() / 2).rms;
    float limit = std::fmax(ref_rms * 4.0f, 0.3f);  // +12 dB, or -10 dBFS
    float worst_rms = 0.0f;
    // Each knob swept 0 -> 1 over 2 s, the others neutral.
    for (int k = 0; k < 3; ++k) {
      auto in = Music(2.0f, 31 + mode * 3 + k);
      const size_t blocks = in.size() / kBlockSize;
      auto out = m.Render(in, [&](size_t b) {
        Controls c = DefaultControls();
        float v = float(b) / float(blocks - 1);
        (k == 0 ? c.bal : k == 1 ? c.fbk : c.rvb) = v;
        c.trigger = (b % 500) == 0;
        c.gate = (b % 500) < 200;
        return c;
      });
      Stats s = Measure(out);
      CHECK(s.finite, "%s knob %d sweep: NaN/inf", name, k);
      CHECK(s.peak <= 1.0f, "%s knob %d sweep: clip", name, k);
      for (size_t i = 0; i + 9600 <= out.size(); i += 9600) {
        worst_rms = std::fmax(worst_rms, Measure(out, i, i + 9600).rms);
      }
    }
    // 3 s of random CV on all three, a new value every block.
    Random r;
    r.Seed(4242 + mode);
    auto in = Music(3.0f, 77 + mode);
    auto out = m.Render(in, [&](size_t b) {
      Controls c = DefaultControls();
      c.bal = r.Float(); c.fbk = r.Float(); c.rvb = r.Float();
      c.trigger = (b % 500) == 0;
      c.gate = (b % 500) < 200;
      return c;
    });
    Stats s = Measure(out);
    CHECK(s.finite, "%s random CV: NaN/inf", name);
    CHECK(s.peak <= 1.0f, "%s random CV: clip", name);
    CHECK(std::fabs(s.dc) < 0.02f, "%s random CV: DC %.3f", name, s.dc);
    for (size_t i = 0; i + 9600 <= out.size(); i += 9600) {
      worst_rms = std::fmax(worst_rms, Measure(out, i, i + 9600).rms);
    }
    // Back to neutral: still finite and sane.
    auto back = m.Render(Music(1.0f, 5), [](size_t) { return DefaultControls(); });
    CHECK(Measure(back).finite, "%s after random CV", name);
    std::printf("    %-11s neutral rms %6.1f dB, loudest 0.2 s with the knobs %6.1f dB\n",
                name, Db(ref_rms), Db(worst_rms));
    CHECK(worst_rms <= limit, "%s gets too loud: %.1f dB vs %.1f dB", name,
          Db(worst_rms), Db(ref_rms));
  }
  return true;
}

// Every knob does something on every mode: its extreme setting changes the
// output by a clear amount (more than -40 dB of difference) compared with the
// neutral one, on music with a moving PITCH and POSITION. (Each mode's own test checks
// what the change is; this one catches dead knobs.)
MONSOON_TEST(every_mode_bal_fbk_rvb_each_change_the_sound) {
  int dead = 0;
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    const char* name = Engine::mode_name(ModeId(mode));
    auto render = [&](int knob, float value) {
      Module m;
      m.SetMode(ModeId(mode));
      auto in = Music(4.0f, 8);
      return m.Render(in, [&](size_t b) {
        Controls c = DefaultControls();
        // PITCH steps every 0.25 s and the sliders sit off-centre, so
        // controls that act on movement (glides, paths) have something to do.
        c.pitch = (b / 375) % 2 ? 7.0f : 0.0f;
        c.density = 0.65f;
        c.position = 0.3f + 0.2f * float((b / 500) % 3);  // TRIG hits new keys
        if (knob == 0) c.bal = value;
        if (knob == 1) c.fbk = value;
        if (knob == 2) c.rvb = value;
        c.trigger = (b % 500) == 0;
        c.gate = (b % 500) < 200;
        return c;
      });
    };
    auto neutral = render(-1, 0.0f);
    float ref = Measure(neutral, neutral.size() / 3).rms;
    const char* names[4] = { "BAL left", "BAL right", "FBK", "RVB" };
    float change[4];
    for (int t = 0; t < 4; ++t) {
      auto out = t < 2 ? render(0, t == 0 ? 0.0f : 1.0f) : render(t - 1, 1.0f);
      std::vector<FloatFrame> d(out.size());
      for (size_t i = 0; i < out.size(); ++i) {
        d[i].l = out[i].l - neutral[i].l;
        d[i].r = out[i].r - neutral[i].r;
      }
      float diff = Measure(d, d.size() / 3).rms;
      float scale = std::fmax(ref, Measure(out, out.size() / 3).rms);
      change[t] = Db(diff) - Db(scale);
      if (change[t] < -40.0f) {
        std::printf("    %s: %s changes the output by only %.1f dB\n", name, names[t], change[t]);
        ++dead;
      }
    }
    std::printf("    %-11s change vs neutral: BAL- %6.1f  BAL+ %6.1f  FBK %6.1f  RVB %6.1f dB\n",
                name, change[0], change[1], change[2], change[3]);
  }
  CHECK(dead == 0, "%d knob settings do (almost) nothing", dead);
  return true;
}

// BAL at noon with FBK and RVB at zero is v1.0, exactly: same output for the
// same music (to float rounding) and byte-identical save slots, which also
// proves that v1.0 slots load into v1.1 unchanged.
MONSOON_TEST(neutral_knobs_reproduce_v10_and_old_slots_load) {
  CHECK(sizeof(kV10) / sizeof(kV10[0]) == MODE_LAST, "table");
  for (int mode = 0; mode < MODE_LAST; ++mode) {
    const V10Print& p = kV10[mode];
    CHECK(std::strcmp(p.mode, Engine::mode_name(ModeId(mode))) == 0, "order");
    // The slot.
    Module a;
    a.SetMode(ModeId(mode));
    auto in = Music(3.0f, 11);
    a.Render(in, [](size_t b) {
      Controls c = DefaultControls();
      c.freeze = b > 3000;
      c.trigger = b == 3500;
      return c;
    });
    PersistentChunk chunks[kMaxPersistentChunks + 1];
    size_t n = a.engine().GetPersistentData(chunks);
    std::vector<uint8_t> slot;
    for (size_t i = 0; i < n; ++i) {
      const uint8_t* q = static_cast<const uint8_t*>(chunks[i].data);
      slot.insert(slot.end(), q, q + chunks[i].size);
    }
    uint32_t crc = n ? Crc32(slot.data(), slot.size()) : 0;
    CHECK(slot.size() == p.slot_size && crc == p.slot_crc,
          "%s: slot differs from v1.0 (%zu bytes, crc %08x)", p.mode, slot.size(), crc);
    if (n) {
      // Load it (padded to 4 bytes per chunk, as in flash).
      std::vector<uint8_t> padded;
      for (size_t i = 0; i < n; ++i) {
        const uint8_t* q = static_cast<const uint8_t*>(chunks[i].data);
        padded.insert(padded.end(), q, q + chunks[i].size);
        while (padded.size() & 3) padded.push_back(0);
      }
      Module b;
      b.engine().RequestLoad(padded.data(), padded.size());
      auto o = b.Render(Silence(0.5f), [](size_t) { return DefaultControls(); });
      CHECK(b.engine().mode() == mode && !b.engine().init_failed(), "%s load", p.mode);
      CHECK(Measure(o).finite, "%s load output", p.mode);
    }
    // The sound.
    Module m;
    m.engine().set_mono_bass(false, true);
    m.SetMode(ModeId(mode));
    auto music = Music(2.0f, 5);
    auto out = m.Render(music, [](size_t b) {
      Controls c = DefaultControls();
      c.trigger = (b % 750) == 0;
      c.gate = (b % 750) < 300;
      return c;
    });
    double el = 0, er = 0, p1 = 0, p2 = 0;
    Random r;
    r.Seed(99);
    for (size_t i = 0; i < out.size(); ++i) {
      el += out[i].l * out[i].l;
      er += out[i].r * out[i].r;
      p1 += out[i].l * r.Bipolar();
      p2 += out[i].r * r.Bipolar();
    }
    auto rel = [](double x, double ref) {
      return std::fabs(x - ref) / std::fmax(std::fabs(ref), 1.0);
    };
    double worst = std::fmax(std::fmax(rel(el, p.energy_l), rel(er, p.energy_r)),
                             std::fmax(rel(p1, p.proj_l), rel(p2, p.proj_r)));
    std::printf("    %-11s slot %s, output vs v1.0 within %.1e\n", p.mode,
                n ? "identical" : "(none)  ", worst);
    CHECK(worst < 1e-4, "%s: neutral knobs no longer sound like v1.0", p.mode);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Envelope page: WET and the three per-mode knobs.

MONSOON_TEST(envelope_routes_to_wet_and_the_mode_knobs) {
  for (int target = 0; target < ENV_TARGET_LAST; ++target) {
    for (int sign = -1; sign <= 1; sign += 2) {
      Module m;
      EnvRouting r;
      for (int i = 0; i < ENV_TARGET_LAST; ++i) r.amount[i] = 0.0f;
      r.amount[target] = float(sign);
      m.engine().set_env_routing(r);
      Controls base = DefaultControls();
      base.blend = 0.5f; base.bal = 0.5f; base.fbk = 0.5f; base.rvb = 0.5f;
      auto loud = Noise(0.3f, 0.5f);
      m.Render(loud, [&](size_t) { return base; });
      const Controls& seen = m.engine().last_controls();
      const float v[4] = { seen.blend, seen.bal, seen.fbk, seen.rvb };
      for (int i = 0; i < ENV_TARGET_LAST; ++i) {
        if (i == target) {
          CHECK(sign > 0 ? v[i] > 0.9f : v[i] < 0.1f,
                "target %d sign %d: %.2f", target, sign, v[i]);
        } else {
          CHECK(v[i] == 0.5f, "target %d moved knob %d", target, i);
        }
      }
      // Silence: the push goes away again.
      m.Render(Silence(2.0f), [&](size_t) { return base; });
      const Controls& calm = m.engine().last_controls();
      const float w[4] = { calm.blend, calm.bal, calm.fbk, calm.rvb };
      CHECK(std::fabs(w[target] - 0.5f) < 0.01f, "target %d stuck at %.2f", target, w[target]);
    }
  }
  // Audibly: on GRAINS, RVB routed +1 opens the FDN reverb on loud input.
  float tail[2];
  for (int k = 0; k < 2; ++k) {
    Module m;
    EnvRouting r = {{ 0.0f, 0.0f, 0.0f, k ? 1.0f : 0.0f }};
    m.engine().set_env_routing(r);
    auto in = Noise(1.0f, 0.5f);
    auto tail_in = Silence(1.0f);
    in.insert(in.end(), tail_in.begin(), tail_in.end());
    auto out = m.Render(in, [](size_t) {
      Controls c = DefaultControls();
      c.blend = 0.0f;  // dry only, so what rings on is the reverb
      return c;
    });
    tail[k] = Measure(out, 48000 + 2400, 48000 + 24000).rms;
  }
  std::printf("    GRAINS, dry, tail after the noise: %.1f dB unrouted, %.1f dB with ENV -> RVB\n",
              Db(tail[0]), Db(tail[1]));
  CHECK(Db(tail[1]) - Db(tail[0]) > 30.0f, "envelope -> RVB should open the reverb");
  return true;
}

// ---------------------------------------------------------------------------
// Settings written by v1.0 still load.

MONSOON_TEST(settings_from_v10_load_and_mono_bass_persists) {
  // The v1.0 layout, field by field.
  CHECK(sizeof(SettingsData) == 220, "size %zu", sizeof(SettingsData));
  CHECK(offsetof(SettingsData, magic) == 60, "magic");
  CHECK(offsetof(SettingsData, mode) == 64, "mode");
  CHECK(offsetof(SettingsData, display) == 65, "display");
  CHECK(offsetof(SettingsData, option) == 66, "option");
  CHECK(offsetof(SettingsData, env) == 96, "env");
  CHECK(offsetof(SettingsData, mono_bass_off) == 216, "flag lives in old padding");

  // A block as v1.0 wrote it: padding zero.
  SettingsData v10;
  std::memset(&v10, 0, sizeof(v10));
  v10.calibration_data.pitch_offset = 66.0f;
  v10.calibration_data.pitch_scale = -84.0f;
  v10.magic = kSettingsMagic;
  v10.mode = MODE_TAPE;
  v10.display = 1;
  v10.option[MODE_TAPE] = 1;
  v10.env[MODE_HALL][ENV_TARGET_RVB] = -2;
  v10.env[MODE_HALL][ENV_TARGET_BAL] = 1;
  uint8_t flash[sizeof(SettingsData)];
  std::memcpy(flash, &v10, sizeof(v10));

  SettingsData d;
  std::memcpy(&d, flash, sizeof(d));
  CHECK(UpgradeSettingsData(&d), "v1.0 block rejected");
  CHECK(d.version == kSettingsVersion, "version");
  CHECK(d.mono_bass_off == 0, "mono bass on by default");
  CHECK(d.mode == MODE_TAPE && d.display == 1 && d.option[MODE_TAPE] == 1,
        "mode/option lost");
  CHECK(d.calibration_data.pitch_scale == -84.0f, "calibration lost");
  EnvRouting r = SettingsEnvRouting(d, MODE_HALL);
  CHECK(r.amount[ENV_TARGET_RVB] == -1.0f && r.amount[ENV_TARGET_BAL] == 0.5f,
        "envelope routing lost");

  // Switch mono bass off, "save" (bytes), "load": it stays off.
  d.mono_bass_off = 1;
  std::memcpy(flash, &d, sizeof(d));
  SettingsData e;
  std::memcpy(&e, flash, sizeof(e));
  CHECK(UpgradeSettingsData(&e) && e.mono_bass_off == 1, "mono bass setting lost");

  // And the engine follows it.
  Module m;
  CHECK(m.engine().mono_bass(), "engine default on");
  m.engine().set_mono_bass(e.mono_bass_off == 0);
  CHECK(!m.engine().mono_bass(), "engine off");

  // Not ours: rejected.
  e.magic = 0x12345678;
  CHECK(!UpgradeSettingsData(&e), "foreign block accepted");
  return true;
}
