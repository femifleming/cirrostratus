// Host tests for the fletchfoorth engine (stages/dobbel.cc).
//   make -f stages/test/dobbel.mk test
// Runs every type and flavour, checks the numbers the manual promises.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "stages/dobbel.h"
#include "stmlib/utils/random.h"

using namespace stages;
using namespace stages::dobbel;
using namespace stmlib;

static int failures = 0;
static int passes = 0;

#define CHECK(cond, ...) do { \
    if (cond) { ++passes; } else { \
      ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
  } while (0)

static Memory memory;
static Engine engine;

struct Rig {
  Config config;
  Input in[kNumSegments];
  GateFlags gates[kNumSegments][kMaxBlockSize];
  bool gate_high[kNumSegments];
  float out[kNumSegments][kMaxBlockSize];

  Rig() {
    config.Init();
    for (int i = 0; i < kNumSegments; ++i) {
      config.segment[i].type = TYPE_RANDOM;
      memset(&in[i], 0, sizeof(Input));
      in[i].slider = 0.5f;
      in[i].pot = 0.5f;
      in[i].cv_slider = 0.5f;
      in[i].gate = gates[i];
      gate_high[i] = false;
    }
    engine.Init(&memory);
  }

  void Set(int i, Type type, int flavour) {
    config.segment[i].type = type;
    config.segment[i].flavour[type] = flavour;
  }
  void Slider(int i, float v) { in[i].slider = v; in[i].cv_slider = v + in[i].cv; }
  void Cv(int i, float v) { in[i].cv = v; in[i].cv_raw = v; in[i].cv_slider = in[i].slider + v; }

  // One block of 8; gate levels follow gate_high (edges made here).
  void Block() {
    for (int i = 0; i < kNumSegments; ++i) {
      for (size_t n = 0; n < kMaxBlockSize; ++n) {
        gates[i][n] = ExtractGateFlags(n == 0 ? last_[i] : gate_high[i], gate_high[i]);
      }
      last_[i] = gate_high[i];
    }
    engine.Process(config, in, out, kMaxBlockSize);
  }

  // Runs for `seconds`, collecting segment i's output.
  std::vector<float> Run(int i, float seconds) {
    std::vector<float> v;
    const int blocks = static_cast<int>(seconds * kSampleRate / kMaxBlockSize);
    for (int b = 0; b < blocks; ++b) {
      Block();
      for (size_t n = 0; n < kMaxBlockSize; ++n) v.push_back(out[i][n]);
    }
    return v;
  }
  bool last_[kNumSegments] = {};

  static GateFlags ExtractGateFlags(bool previous, bool current) {
    uint8_t f = current ? GATE_FLAG_HIGH : GATE_FLAG_LOW;
    if (current && !previous) f |= GATE_FLAG_RISING;
    if (!current && previous) f |= GATE_FLAG_FALLING;
    return static_cast<GateFlags>(f);
  }
};

static float Frequency(const std::vector<float>& v) {
  // rising zero crossings, after removing the mean
  double mean = 0;
  for (float x : v) mean += x;
  mean /= v.size();
  int first = -1, last = -1, count = 0;
  for (size_t n = 1; n < v.size(); ++n) {
    if (v[n - 1] - mean < 0 && v[n] - mean >= 0) {
      if (first < 0) first = n; else { last = n; ++count; }
    }
  }
  if (count < 1) return 0.0f;
  return count * kSampleRate / (last - first);
}

static int Changes(const std::vector<float>& v) {
  int c = 0;
  for (size_t n = 1; n < v.size(); ++n) if (v[n] != v[n - 1]) ++c;
  return c;
}

static int RisingCrossings(const std::vector<float>& v, float threshold) {
  int c = 0;
  for (size_t n = 1; n < v.size(); ++n) if (v[n - 1] < threshold && v[n] >= threshold) ++c;
  return c;
}

static void TestEveryFlavourRuns() {
  for (int t = 0; t < TYPE_LAST; ++t) {
    for (int f = 0; f < kNumFlavours[t]; ++f) {
      Rig r;
      for (int i = 0; i < kNumSegments; ++i) {
        r.Set(i, static_cast<Type>(t), f);
        r.Slider(i, 0.3f + 0.1f * i);
        r.in[i].pot = 0.15f * i;
        r.Cv(i, 0.05f * sinf(i));
      }
      bool ok = true;
      for (int b = 0; b < 40000; ++b) {  // ~10 s
        if (b % 500 == 0) for (int i = 0; i < kNumSegments; ++i) r.gate_high[i] = !r.gate_high[i];
        for (int i = 0; i < kNumSegments; ++i) r.in[i].cv_raw = 0.5f * sinf(b * 0.37f + i);
        r.Block();
        for (int i = 0; i < kNumSegments; ++i)
          for (size_t n = 0; n < kMaxBlockSize; ++n)
            if (!(fabsf(r.out[i][n]) <= 1.25f)) ok = false;
      }
      CHECK(ok, "type %d flavour %d: output not finite / out of range", t, f);
    }
  }
}

static void TestOscillatorPitch() {
  // slider 0.625 = C1 + 45 semitones = A4 = 440 Hz (aux 128 = fine ~0)
  for (int f = 0; f < OSC_LAST; ++f) {
    if (f == OSC_FM || f == OSC_GENDY || f == OSC_RIDER || f == OSC_PAIR) continue;
    Rig r;
    r.Set(0, TYPE_OSCILLATOR, f);
    r.config.segment[0].aux[TYPE_OSCILLATOR] = 127;  // fine = -0.004 semitone
    r.Slider(0, 45.0f / 72.0f);
    r.in[0].pot = f == OSC_ADDITIVE ? 0.0f : (f == OSC_BANK ? 0.0f : 0.0f);
    std::vector<float> v = r.Run(0, 1.0f);
    const float hz = Frequency(v);
    CHECK(fabsf(hz - 440.0f) < 2.0f, "flavour %d: A4 measured %.2f Hz", f, hz);
  }
  // 1 V/oct: +1 V (0.125) doubles it
  Rig r;
  r.Set(0, TYPE_OSCILLATOR, OSC_MORPH);
  r.in[0].pot = 0.0f;
  r.Slider(0, 45.0f / 72.0f);
  r.Cv(0, 0.125f);
  const float hz = Frequency(r.Run(0, 1.0f));
  CHECK(fabsf(hz - 880.0f) < 3.0f, "V/oct: +1 V gave %.2f Hz", hz);
  // amplitude: about +/- 5 V
  Rig a;
  a.Set(0, TYPE_OSCILLATOR, OSC_MORPH);
  a.in[0].pot = 0.0f;
  std::vector<float> v = a.Run(0, 0.5f);
  float peak = 0;
  for (float x : v) peak = fmaxf(peak, fabsf(x));
  CHECK(fabsf(peak - kAudioPeak) < 0.02f, "sine peak %.3f (want 0.625 = 5 V)", peak);
}

static void TestDifferencePair() {
  Rig r;
  r.Set(0, TYPE_OSCILLATOR, OSC_PAIR);
  r.Slider(0, 45.0f / 72.0f);
  r.in[0].pot = 0.0f;  // unison: a plain sine at 440
  CHECK(fabsf(Frequency(r.Run(0, 1.0f)) - 440.0f) < 2.0f, "pair at unison is not 440 Hz");
}

static void TestVoiceMode() {
  Rig r;
  r.Set(0, TYPE_OSCILLATOR, OSC_MORPH);
  r.config.segment[0].alt = 1 << TYPE_OSCILLATOR;
  r.in[0].patched = true;
  r.in[0].pot = 0.0f;
  r.gate_high[0] = false;
  std::vector<float> silent = r.Run(0, 0.5f);
  float peak = 0;
  for (float x : silent) peak = fmaxf(peak, fabsf(x));
  CHECK(peak < 0.01f, "voice mode, gate low: should be silent, peak %.3f", peak);
  r.gate_high[0] = true;
  std::vector<float> open = r.Run(0, 0.2f);
  peak = 0;
  for (size_t n = open.size() / 2; n < open.size(); ++n) peak = fmaxf(peak, fabsf(open[n]));
  CHECK(peak > 0.6f, "voice mode, gate high: should play, peak %.3f", peak);
  r.gate_high[0] = false;
  std::vector<float> tail = r.Run(0, 2.0f);
  peak = 0;
  for (size_t n = tail.size() - 1000; n < tail.size(); ++n) peak = fmaxf(peak, fabsf(tail[n]));
  CHECK(peak < 0.01f, "voice mode, released: should fade, peak %.3f", peak);
}

static void TestEnvelope() {
  // slider 0.25 -> 0.001 * 10000^0.25 = 10 ms attack; pot 0.5 -> 100 ms decay
  Rig r;
  r.Set(0, TYPE_ENVELOPE, ENVELOPE_ONCE);
  r.in[0].patched = true;
  r.Slider(0, 0.25f);
  r.in[0].pot = 0.5f;
  r.Run(0, 0.05f);
  r.gate_high[0] = true;
  r.Block();
  r.gate_high[0] = false;  // a trigger: one block (0.26 ms)
  std::vector<float> v = r.Run(0, 0.5f);
  size_t peak_at = 0;
  for (size_t n = 0; n < v.size(); ++n) if (v[n] > v[peak_at]) peak_at = n;
  const float attack_ms = (peak_at + 8) * 1000.0f / kSampleRate;
  CHECK(fabsf(v[peak_at] - 1.0f) < 0.001f, "envelope peak %.3f (want 1.0 = 8 V)", v[peak_at]);
  CHECK(fabsf(attack_ms - 10.0f) < 1.0f, "attack %.2f ms (want 10)", attack_ms);
  size_t end = peak_at;
  while (end < v.size() && v[end] > 0.0f) ++end;
  const float decay_ms = (end - peak_at) * 1000.0f / kSampleRate;
  CHECK(fabsf(decay_ms - 100.0f) < 3.0f, "decay %.2f ms (want 100)", decay_ms);

  // a held gate sustains at the top
  Rig g;
  g.Set(0, TYPE_ENVELOPE, ENVELOPE_ONCE);
  g.in[0].patched = true;
  g.Slider(0, 0.0f);
  g.gate_high[0] = true;
  std::vector<float> held = g.Run(0, 0.3f);
  CHECK(held.back() == 1.0f, "held gate should sustain at 1.0, got %.3f", held.back());
  g.gate_high[0] = false;
  g.in[0].pot = 0.0f;
  std::vector<float> after = g.Run(0, 0.05f);
  CHECK(after.back() == 0.0f, "release should fall to 0, got %.3f", after.back());

  // ratchet: three hits from one trigger
  Rig t;
  t.Set(0, TYPE_ENVELOPE, ENVELOPE_THRICE);
  t.in[0].patched = true;
  t.Slider(0, 0.1f);
  t.in[0].pot = 0.1f;
  t.gate_high[0] = true;
  t.Block();
  t.gate_high[0] = false;
  std::vector<float> rv = t.Run(0, 1.0f);
  CHECK(RisingCrossings(rv, 0.9f) == 3, "ratchet x3 made %d hits", RisingCrossings(rv, 0.9f));

  // loop with nothing patched: cycles by itself
  Rig l;
  l.Set(0, TYPE_ENVELOPE, ENVELOPE_ONCE);
  l.config.segment[0].alt = 1 << TYPE_ENVELOPE;
  l.Slider(0, 0.25f);
  l.in[0].pot = 0.25f;  // 10 ms + 10 ms -> 50 Hz
  const int cycles = RisingCrossings(l.Run(0, 1.0f), 0.5f);
  CHECK(cycles >= 47 && cycles <= 51, "looping envelope made %d cycles in 1 s (want ~50)", cycles);

  // CV: +1 V halves both times
  Rig c;
  c.Set(0, TYPE_ENVELOPE, ENVELOPE_ONCE);
  c.config.segment[0].alt = 1 << TYPE_ENVELOPE;
  c.Slider(0, 0.25f);
  c.in[0].pot = 0.25f;
  c.Cv(0, 0.125f);
  const int fast = RisingCrossings(c.Run(0, 1.0f), 0.5f);
  CHECK(fast >= 95 && fast <= 101, "envelope +1 V: %d cycles (want ~100)", fast);
}

static void TestRandom() {
  // free running: slider 0.5 -> 0.05 * 2000^0.5 = 2.24 Hz
  Rig r;
  r.Set(0, TYPE_RANDOM, RANDOM_UNIFORM);
  r.in[0].pot = 0.0f;  // no slew: steps
  r.Slider(0, 0.5f);
  std::vector<float> v = r.Run(0, 20.0f);
  const float rate = Changes(v) / 20.0f;
  CHECK(fabsf(rate - 2.236f) < 0.15f, "uniform random at slider 0.5: %.2f changes/s (want 2.24)", rate);
  float lo = 1, hi = -1;
  for (float x : v) { lo = fminf(lo, x); hi = fmaxf(hi, x); }
  CHECK(lo >= 0.0f && hi <= 1.0f && hi - lo > 0.8f, "unipolar range %.2f..%.2f", lo, hi);

  Rig b;
  b.Set(0, TYPE_RANDOM, RANDOM_UNIFORM);
  b.config.segment[0].alt = 1 << TYPE_RANDOM;
  b.Slider(0, 0.8f);
  b.in[0].pot = 0.0f;
  std::vector<float> bv = b.Run(0, 10.0f);
  lo = 1; hi = -1;
  for (float x : bv) { lo = fminf(lo, x); hi = fmaxf(hi, x); }
  CHECK(lo < -0.5f && hi > 0.5f && lo >= -0.626f && hi <= 0.626f, "bipolar range %.2f..%.2f (want +/-5 V)", lo, hi);

  // clocked: slider at 0 = never change (locked), 1 = every clock
  Rig c;
  c.Set(0, TYPE_RANDOM, RANDOM_UNIFORM);
  c.in[0].patched = true;
  c.in[0].pot = 0.0f;
  c.Slider(0, 0.0f);
  int changes_locked = 0;
  float prev = -1;
  for (int k = 0; k < 50; ++k) {
    c.gate_high[0] = true; c.Block();
    c.gate_high[0] = false;
    for (int j = 0; j < 20; ++j) c.Block();
    if (prev >= 0 && c.out[0][7] != prev) ++changes_locked;
    prev = c.out[0][7];
  }
  CHECK(changes_locked == 0, "clocked with slider 0 should be locked, changed %d times", changes_locked);
  c.Slider(0, 1.0f);
  int changes = 0;
  for (int k = 0; k < 50; ++k) {
    c.gate_high[0] = true; c.Block();
    c.gate_high[0] = false;
    for (int j = 0; j < 20; ++j) c.Block();
    if (c.out[0][7] != prev) ++changes;
    prev = c.out[0][7];
  }
  CHECK(changes >= 49, "clocked with slider 1 should change every clock, %d/50", changes);

  // quantised: every value a whole semitone (1/12 V = 1/96)
  Rig q;
  q.Set(0, TYPE_RANDOM, RANDOM_QUANTISED);
  q.in[0].pot = 0.0f;
  q.Slider(0, 0.8f);
  std::vector<float> qv = q.Run(0, 5.0f);
  bool whole = true;
  for (float x : qv) {
    const float semis = x * 96.0f;
    if (fabsf(semis - roundf(semis)) > 0.01f) { if (whole) printf("  off-grid %.5f\n", semis); whole = false; }
  }
  CHECK(whole, "quantised random is not on semitones");

  // slew: smooth with the pot up (many small changes)
  Rig s;
  s.Set(0, TYPE_RANDOM, RANDOM_UNIFORM);
  s.in[0].pot = 1.0f;
  s.Slider(0, 0.5f);
  std::vector<float> sv = s.Run(0, 5.0f);
  float max_jump = 0;
  for (size_t n = 1; n < sv.size(); ++n) max_jump = fmaxf(max_jump, fabsf(sv[n] - sv[n - 1]));
  CHECK(max_jump < 0.001f, "slewed random jumps %.4f per sample", max_jump);
}

static void TestOrbitAndKin() {
  Rig r;
  for (int i = 0; i < 3; ++i) r.Set(i, TYPE_RANDOM, RANDOM_ORBIT);
  r.in[0].pot = 0.0f;  // circle
  r.Slider(0, 0.8f);
  r.Run(0, 0.3f);
  // on a circle x^2 + y^2 = 1, so (2x-1)^2 + (2y-1)^2 = 1 in 0-1 terms
  bool circle = true;
  for (int k = 0; k < 200; ++k) {
    r.Block();
    const float x = -(r.out[0][0] * 2.0f - 1.0f);  // seg 0 outputs -x? leader outputs x
    (void)x;
    const float a = r.out[0][0] * 2.0f - 1.0f;
    const float b = r.out[1][0] * 2.0f - 1.0f;
    if (fabsf(a * a + b * b - 1.0f) > 0.02f) circle = false;
  }
  CHECK(circle, "orbit run of 3 (circle): X and Y are not on a circle");

  Rig k;
  k.Set(0, TYPE_RANDOM, RANDOM_UNIFORM);
  k.Set(1, TYPE_RANDOM, RANDOM_KIN);
  k.in[0].pot = 0.0f;
  k.Slider(0, 0.5f);
  k.Slider(1, 0.9f);
  k.in[1].pot = 0.1f;  // tight
  double err = 0;
  int count = 0;
  for (int b = 0; b < 40000; ++b) {
    k.Block();
    if (b > 2000) { err += fabs(k.out[1][0] - k.out[0][0]); ++count; }
  }
  CHECK(err / count < 0.08, "kin strays %.3f from its left neighbour", err / count);
}

static void TestChaos() {
  // Turing locked: the pattern repeats every 8 ticks
  Rig t;
  t.Set(0, TYPE_CHAOS, CHAOS_TURING);
  t.in[0].patched = true;
  t.in[0].pot = 0.0f;
  std::vector<float> values;
  for (int k = 0; k < 40; ++k) {
    t.gate_high[0] = true; t.Block();
    t.gate_high[0] = false;
    for (int j = 0; j < 400; ++j) t.Block();
    values.push_back(t.out[0][7]);
  }
  bool loops = true;
  for (int k = 8; k < 40; ++k) if (fabsf(values[k] - values[k - 8]) > 0.01f) loops = false;
  CHECK(loops, "Turing loop with pot left should repeat every 8 steps");

  // logistic: r = 3.4 settles into a 2-cycle, r = 4 doesn't repeat
  Rig l;
  l.Set(0, TYPE_CHAOS, CHAOS_LOGISTIC);
  l.in[0].patched = true;
  l.in[0].pot = 0.0f;
  std::vector<float> lv;
  for (int k = 0; k < 200; ++k) {
    l.gate_high[0] = true; l.Block();
    l.gate_high[0] = false;
    for (int j = 0; j < 400; ++j) l.Block();
    lv.push_back(l.out[0][7]);
  }
  CHECK(fabsf(lv[199] - lv[197]) < 0.01f && fabsf(lv[199] - lv[198]) > 0.1f,
        "logistic r=3.4 should settle into a 2-cycle");

  // Lorenz run of three: bounded, three different signals, keeps moving
  Rig z;
  for (int i = 0; i < 3; ++i) z.Set(i, TYPE_CHAOS, CHAOS_LORENZ);
  z.Slider(0, 0.8f);
  z.in[0].pot = 0.3f;  // rho 29: chaotic
  std::vector<float> zx = z.Run(0, 10.0f);
  float lo = 1, hi = 0;
  for (size_t n = zx.size() / 2; n < zx.size(); ++n) { lo = fminf(lo, zx[n]); hi = fmaxf(hi, zx[n]); }
  CHECK(hi - lo > 0.3f && lo >= 0.0f && hi <= 1.0f, "Lorenz x range %.2f..%.2f", lo, hi);
  CHECK(z.out[0][0] != z.out[1][0] && z.out[1][0] != z.out[2][0], "Lorenz run outputs should differ");

  // coin: probability 0 never, 1 always
  for (int p = 0; p <= 1; ++p) {
    Rig c;
    c.Set(0, TYPE_CHAOS, CHAOS_COIN);
    c.in[0].pot = static_cast<float>(p);
    c.Slider(0, 0.7f);  // 10 Hz
    const int gates = RisingCrossings(c.Run(0, 10.0f), 0.5f);
    if (p == 0) CHECK(gates == 0, "coin p=0 gave %d gates", gates);
    else CHECK(gates >= 95 && gates <= 106, "coin p=1 at ~10.5 Hz gave %d gates in 10 s", gates);
  }

  // dust: density from the slider (top = 50 per second)
  Rig d;
  d.Set(0, TYPE_CHAOS, CHAOS_DUST);
  d.Slider(0, 1.0f);
  d.in[0].pot = 0.0f;
  const int dust = RisingCrossings(d.Run(0, 20.0f), 0.5f);
  // 50 a second, but triggers closer than 5 ms merge: about 39 a second land
  CHECK(dust > 680 && dust < 880, "dust at slider top: %d in 20 s (want ~780)", dust);

  // cellular run of two: different cells, both moving
  Rig ca;
  ca.Set(0, TYPE_CHAOS, CHAOS_CELLULAR);
  ca.Set(1, TYPE_CHAOS, CHAOS_CELLULAR);
  ca.Slider(0, 0.7f);
  ca.in[0].pot = 0.0f;  // rule 30
  std::vector<float> c0 = ca.Run(0, 3.0f);
  CHECK(Changes(c0) > 20, "cellular automaton not moving (%d changes)", Changes(c0));
}

static void TestProcessors() {
  // follower: a 5 V sine in gives ~full output at x1
  Rig f;
  f.Set(0, TYPE_PROCESSOR, PROC_FOLLOW);
  f.Slider(0, 0.3f);
  f.in[0].pot = 0.0f;
  float out = 0;
  for (int b = 0; b < 20000; ++b) {
    f.in[0].cv_raw = kAudioPeak * sinf(b * 2.0f * 3.14159265f * 200.0f / 3906.25f);
    f.Block();
    out = f.out[0][7];
  }
  CHECK(out > 0.5f && out <= 1.0f, "follower, 5 V sine: %.2f", out);
  f.in[0].patched = true;
  f.gate_high[0] = true;  // freeze
  f.in[0].cv_raw = 0.0f;
  f.Run(0, 1.0f);
  CHECK(fabsf(f.out[0][7] - out) < 0.15f, "follower freeze moved %.2f -> %.2f", out, f.out[0][7]);

  // transient: three clicks, three triggers
  Rig t;
  t.Set(0, TYPE_PROCESSOR, PROC_TRANSIENT);
  t.Slider(0, 0.2f);
  t.in[0].pot = 0.5f;
  std::vector<float> tv;
  for (int b = 0; b < 3 * 4000; ++b) {
    const int k = (b + 2000) % 4000;  // clicks in the middle, not at sample 0
    t.in[0].cv_raw = k < 40 ? kAudioPeak * (k % 2 ? 1.0f : -1.0f) * (1.0f - k / 40.0f) : 0.0f;
    t.Block();
    for (size_t n = 0; n < kMaxBlockSize; ++n) tv.push_back(t.out[0][n]);
  }
  CHECK(RisingCrossings(tv, 0.3f) == 3, "transient: %d triggers for 3 clicks", RisingCrossings(tv, 0.3f));

  // slew: a step takes time with the slider up, none with it down
  Rig s;
  s.Set(0, TYPE_PROCESSOR, PROC_SLEW);
  s.Slider(0, 0.4f);  // rise 0.8 s
  s.in[0].pot = 0.0f;
  s.Cv(0, 0.5f);
  s.in[0].cv_slider = 0.4f;
  std::vector<float> sv = s.Run(0, 0.8f);
  CHECK(sv.back() > 0.25f && sv.back() < 0.4f, "slew: after one time constant %.3f (want ~0.32)", sv.back());

  // looper: record a ramp, play it back the same
  Rig l;
  l.Set(0, TYPE_PROCESSOR, PROC_LOOPER);
  l.in[0].patched = true;
  l.Slider(0, 0.5f);  // 1x
  l.in[0].pot = 0.0f;
  l.gate_high[0] = true;
  const int rec_blocks = static_cast<int>(2.0f * kSampleRate / 8);
  for (int b = 0; b < rec_blocks; ++b) {
    l.Cv(l.in[0].cv_slider == 0 ? 0 : 0, 0);  // keep slider
    l.in[0].cv = 0.5f * b / rec_blocks;
    l.Block();
  }
  l.gate_high[0] = false;
  l.in[0].cv = 0.0f;
  std::vector<float> lv = l.Run(0, 2.0f);
  // the loop is 2 s: a ramp 0 -> 0.5, so mid-playback about 0.25
  const float mid = lv[lv.size() / 2];
  CHECK(fabsf(mid - 0.25f) < 0.05f, "looper playback mid-loop %.3f (want ~0.25)", mid);
  CHECK(lv.front() < 0.05f, "looper starts at the loop start, got %.3f", lv.front());

  // crush: output stays within +/- 10 V for a hot input
  Rig c;
  c.Set(0, TYPE_PROCESSOR, PROC_CRUSH);
  c.Slider(0, 1.0f);
  c.in[0].pot = 0.5f;
  c.config.segment[0].aux[TYPE_PROCESSOR] = 255;
  bool ok = true;
  for (int b = 0; b < 8000; ++b) {
    c.in[0].cv_raw = sinf(b * 0.1f);
    c.Block();
    if (fabsf(c.out[0][0]) > 1.26f) ok = false;
  }
  CHECK(ok, "crush output out of range");
}

static void TestNeighbours() {
  // FM: the left oscillator modulates; with the pot at 0, plain sine
  Rig r;
  r.Set(0, TYPE_OSCILLATOR, OSC_MORPH);
  r.Set(1, TYPE_OSCILLATOR, OSC_FM);
  r.Slider(0, 0.6f);
  r.Slider(1, 45.0f / 72.0f);
  r.in[0].pot = 0.0f;
  r.in[1].pot = 0.0f;
  CHECK(fabsf(Frequency(r.Run(1, 1.0f)) - 440.0f) < 2.0f, "FM depth 0 should be a plain 440 Hz sine");
  r.in[1].pot = 0.8f;
  std::vector<float> fm = r.Run(1, 0.5f);
  float peak = 0;
  for (float x : fm) peak = fmaxf(peak, fabsf(x));
  CHECK(peak <= kAudioPeak + 0.01f, "FM peak %.3f", peak);

  // rider: draws the left neighbour into its waveform
  Rig w;
  w.Set(0, TYPE_ENVELOPE, ENVELOPE_ONCE);
  w.config.segment[0].alt = 1 << TYPE_ENVELOPE;  // looping envelope as the source
  w.Slider(0, 0.3f);
  w.in[0].pot = 0.3f;
  w.Set(1, TYPE_OSCILLATOR, OSC_RIDER);
  w.Slider(1, 0.5f);
  w.in[1].pot = 0.0f;  // fastest refresh
  std::vector<float> wv = w.Run(1, 2.0f);
  float lo = 1, hi = -1;
  for (size_t n = wv.size() / 2; n < wv.size(); ++n) { lo = fminf(lo, wv[n]); hi = fmaxf(hi, wv[n]); }
  CHECK(hi - lo > 0.5f, "rider should carry the envelope's shape (range %.2f)", hi - lo);
}

static void TestBank() {
  Rig r;
  for (int t = 0; t < kBankTables; ++t) {
    int peak = 0;
    long sum = 0;
    for (int n = 0; n < kBankLength; ++n) {
      peak = std::max(peak, abs(static_cast<int>(memory.bank[t][n])));
      sum += memory.bank[t][n];
    }
    CHECK(peak > 31000 && peak <= 32000, "bank table %d peak %d", t, peak);
    CHECK(labs(sum / kBankLength) < 400, "bank table %d DC %ld", t, sum / kBankLength);
  }
}

static void TestConfig() {
  Config c;
  c.Init();
  CHECK(c.segment[0].type == TYPE_RANDOM && c.segment[4].type == TYPE_ENVELOPE &&
        c.segment[5].type == TYPE_OSCILLATOR, "default layout");
  c.segment[2].type = 9;
  c.segment[3].flavour[TYPE_CHAOS] = 40;
  c.Sanitize();
  CHECK(c.segment[2].type == TYPE_RANDOM && c.segment[3].flavour[TYPE_CHAOS] == 0, "sanitize");
  c.version = 0;
  c.Sanitize();
  CHECK(c.version == 1 && c.segment[5].type == TYPE_OSCILLATOR, "unknown version resets");
  CHECK(sizeof(Config) % 4 == 0, "Config is %zu bytes (keep a multiple of 4)", sizeof(Config));
}

static void WriteWav(const char* path, const std::vector<float>& v) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  const uint32_t sr = 31250, n = v.size(), bytes = n * 2;
  fwrite("RIFF", 1, 4, f); uint32_t x = 36 + bytes; fwrite(&x, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f); x = 16; fwrite(&x, 4, 1, f);
  uint16_t s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
  fwrite(&sr, 4, 1, f); x = sr * 2; fwrite(&x, 4, 1, f); s = 2; fwrite(&s, 2, 1, f);
  s = 16; fwrite(&s, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
  for (float y : v) { int16_t q = static_cast<int16_t>(fmaxf(-1, fminf(1, y)) * 32767); fwrite(&q, 2, 1, f); }
  fclose(f);
}

int main(int argc, char** argv) {
  Random::Seed(1234);
  TestConfig();
  TestBank();
  TestEveryFlavourRuns();
  TestOscillatorPitch();
  TestDifferencePair();
  TestVoiceMode();
  TestEnvelope();
  TestRandom();
  TestOrbitAndKin();
  TestChaos();
  TestProcessors();
  TestNeighbours();

  if (argc > 1) {
    // Renders: one WAV per oscillator flavour, the pot sweeping 0 -> 1.
    for (int f = 0; f < OSC_LAST; ++f) {
      Rig r;
      r.Set(0, TYPE_OSCILLATOR, OSC_GENDY);
      r.Set(1, TYPE_OSCILLATOR, f);
      r.Slider(0, 0.4f);
      r.Slider(1, 0.42f);
      std::vector<float> v;
      for (int b = 0; b < 4 * 3906; ++b) {
        r.in[1].pot = b / (4.0f * 3906.0f);
        r.Block();
        for (size_t n = 0; n < 8; ++n) v.push_back(r.out[1][n] / kAudioPeak * 0.8f);
      }
      char path[256];
      snprintf(path, sizeof(path), "%s/osc_%d.wav", argv[1], f);
      WriteWav(path, v);
    }
  }

  printf("%d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
