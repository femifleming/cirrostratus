// Host-side tests for Sync LFO segments. Drives the real ChainState and
// SegmentGenerator code with simulated buttons and a simulated clock.
// Run with: sh stages/test/links/run.sh

#include <cmath>
#include <cstdio>
#include <cstring>
#include "stages/chain_state.h"
#include "stages/settings.h"
#include "stages/drivers/serial_link.h"
using namespace stages;
using namespace stmlib;

ChainState cs; Settings st; SegmentGenerator sg[kNumChannels];
SerialLink l, r; IOBuffer::Block block; SegmentGenerator::Output out[kBlockSize];
uint8_t pressed = 0; int fails = 0;
const float kBlockRate = kSampleRate / kBlockSize;

// Clock into segment 1's gate: a rising edge every `clock_blocks` blocks.
int clock_blocks = 0; long n = 0;
float val[kNumChannels]; int wraps[kNumChannels];

void run_block() {
  static GateFlags no_gate[kBlockSize] = {0};
  GateFlags clk[kBlockSize];
  bool tick = clock_blocks && (n % clock_blocks) == 0;
  for (size_t j = 0; j < kBlockSize; ++j)
    clk[j] = (clock_blocks && (n % clock_blocks) < 4) ? GATE_FLAG_HIGH : GATE_FLAG_LOW;
  if (tick) clk[0] = GateFlags(GATE_FLAG_HIGH | GATE_FLAG_RISING);
  block.input_patched[0] = clock_blocks != 0;
  ++n;
  cs.set_local_switch_pressed(pressed);
  cs.Update(block, &st, sg, out);
  for (size_t c = 0; c < kNumChannels; ++c) {
    const GateFlags* g = block.input_patched[c] ? clk : no_gate;
    sg[c].Process(g, out, kBlockSize);
    val[c] = out[kBlockSize - 1].value;
    if (cs.sync_lfo(c) && sg[c].take_sync_wrap()) ++wraps[c];
  }
}
void run_blocks(long k) { while (k--) run_block(); }
void tick_ms(int ms) { for (int k = 0; k < ms * 4; ++k) run_block(); }
void check(bool c, const char* what) { printf("%s %s\n", c ? "PASS" : "FAIL", what); if (!c) ++fails; }
uint16_t& cfg(int i) { return st.mutable_state()->segment_configuration[i]; }
void press(int b) { pressed |= 1 << b; } void release(int b) { pressed &= ~(1 << b); }
void tap(int b) { press(b); tick_ms(100); release(b); tick_ms(20); }
void link(int a, int b) { press(a); tick_ms(200); press(b); tick_ms(80); release(b); tick_ms(20); release(a); tick_ms(50); }
void slider(int i, int ratio_index) { block.slider[i] = block.cv_slider[i] = (ratio_index + 0.5f) / 15.0f; }
void clear_wraps() { memset(wraps, 0, sizeof(wraps)); }
bool near(int a, int b) { return a >= b - 1 && a <= b + 1; }

int main() {
  memset(&block, 0, sizeof(block));
  for (size_t i = 0; i < kNumChannels; ++i) {
    sg[i].Init();
    block.slider[i] = block.cv_slider[i] = 0.5f; block.pot[i] = 0.5f;
  }
  st.mutable_state()->multimode = MULTI_MODE_STAGES;
  cs.Init(&l, &r, st);
  tick_ms(2100);

  // 1. Type cycle: ramp, step, hold, follow, sync, ramp.
  tap(0); tap(0); tap(0);
  check(cs.follower(0), "three taps: follower");
  tap(0);
  check(cs.sync_lfo(0) && !cs.follower(0) && (cfg(0) & kFollowBit) && (cfg(0) & 3) == 1,
        "fourth tap: Sync LFO");
  tap(0);
  check(!cs.sync_lfo(0) && !(cfg(0) & kFollowBit) && (cfg(0) & 3) == 0, "fifth tap: back to ramp");

  // Advanced mode: ramp, step, hold, random, follow, sync.
  st.mutable_state()->multimode = MULTI_MODE_STAGES_ADVANCED;
  cs.start_reinit(); tick_ms(2600);
  for (int k = 0; k < 5; ++k) tap(5);
  check(cs.sync_lfo(5), "advanced mode: five taps reach Sync LFO");
  tap(5);
  check(!cs.sync_lfo(5) && (cfg(5) & 0xff) == 0, "advanced mode: then ramp");
  st.mutable_state()->multimode = MULTI_MODE_STAGES;
  cs.start_reinit(); tick_ms(2600);

  // 2. Four Sync LFOs, linked 1-4.
  for (int s = 0; s < 4; ++s) for (int k = 0; k < 4; ++k) tap(s);
  check(cs.sync_lfo(0) && cs.sync_lfo(1) && cs.sync_lfo(2) && cs.sync_lfo(3) && !cs.sync_lfo(4),
        "segments 1-4 are Sync LFOs");
  link(0, 3);
  check(cs.linked(0) && cs.linked(3) && !cs.linked(4), "1-4 linked");

  // 3. Linking a Sync LFO to a ramp does nothing.
  link(3, 4);
  check(!(cfg(3) & kLinkBit) && !cs.linked(4), "Sync LFO + ramp: no link");
  // A stored link across the boundary is ignored.
  cfg(3) |= kLinkBit; tick_ms(10);
  check(!cs.linked(4) && cs.sync_lfo(3), "stored mixed link ignored");
  cfg(3) &= ~kLinkBit; tick_ms(10);

  // 4. Ratios against a clock: 1 clock every 100 blocks (0.0256 s).
  slider(0, 7);   // x1
  slider(1, 8);   // x2
  slider(2, 6);   // /2
  slider(3, 10);  // x4
  clock_blocks = 100;
  run_blocks(1000);  // Settle on the clock.
  clear_wraps();
  run_blocks(1600);  // 16 clocks.
  printf("   wraps x1 %d, x2 %d, /2 %d, x4 %d\n", wraps[0], wraps[1], wraps[2], wraps[3]);
  check(near(wraps[0], 16), "x1: one cycle per clock");
  check(near(wraps[1], 32), "x2: two cycles per clock");
  check(near(wraps[2], 8), "/2: one cycle every two clocks");
  check(near(wraps[3], 64), "x4: four cycles per clock");
  check(sg[0].sync_ratio_class() == 0 && sg[1].sync_ratio_class() == 1 &&
        sg[2].sync_ratio_class() == -1, "ratio classes for the LEDs");
  check(fabsf(sg[0].sync_period() - 800.0f) < 1.0f, "master tracks the clock period");

  // 5. Locked to the clock.
  // Use a rising ramp (shape 3/5) on all four: right after a clock the
  // x1, x2, x4 values are near 0 and /2 is near 0 or 0.5.
  for (int s = 0; s < 4; ++s) block.pot[s] = 0.6f;  // zone 3, blend 0: ramp
  run_blocks(300 - (n % 100));
  run_block();
  printf("   after edge: %.3f %.3f %.3f %.3f\n", val[0], val[1], val[2], val[3]);
  check(val[0] < 0.02f && val[1] < 0.03f && val[3] < 0.06f, "x1, x2, x4 restart on the clock");
  check(val[2] < 0.02f || fabsf(val[2] - 0.5f) < 0.02f, "/2 on a half-cycle boundary");

  // 6. Clock stops: keeps running at the same tempo.
  clock_blocks = 0;
  clear_wraps();
  run_blocks(1600);
  printf("   free wraps x1 %d, x2 %d\n", wraps[0], wraps[1]);
  check(near(wraps[0], 16) && near(wraps[1], 32), "clock stopped: same tempo");

  // 7. Clock restarts at a new tempo: follows it.
  clock_blocks = 50;
  run_blocks(500);
  clear_wraps();
  run_blocks(800);  // 16 clocks.
  check(near(wraps[0], 16) && near(wraps[3], 64), "new tempo followed");

  // 8. Shapes (x1 segment 1, look at a whole cycle).
  float lo, hi;
  auto range = [&](int s, float* plo, float* phi) {
    *plo = 10; *phi = -10;
    for (int k = 0; k < 100; ++k) { run_block(); if (val[s] < *plo) *plo = val[s]; if (val[s] > *phi) *phi = val[s]; }
  };
  block.pot[0] = 0.0f; range(0, &lo, &hi);
  check(lo < 0.02f && hi > 0.97f, "sine: 0 to 8 V");
  block.pot[0] = 0.8f; range(0, &lo, &hi);
  check(lo < 0.02f && hi > 0.98f, "square: 0 to 8 V");
  block.pot[0] = 1.0f; {
    float first = 0; int changes = 0; float prev = -1;
    for (int k = 0; k < 400; ++k) { run_block(); if (prev >= 0 && fabsf(val[0] - prev) > 1e-4f) ++changes; prev = val[0]; if (!k) first = val[0]; }
    printf("   random: %d steps in 8 cycles\n", changes);
    check(changes >= 6 && changes <= 8, "random: a new level each cycle, held in between");
    (void)first;
  }

  // 9. Bipolar: +-5 V.
  cfg(0) |= 0x08; tick_ms(10);
  block.pot[0] = 0.0f; range(0, &lo, &hi);
  printf("   bipolar %.3f..%.3f\n", lo, hi);
  check(lo < -0.6f && hi > 0.6f && hi < 0.63f, "bipolar sine: -5 to +5 V");
  cfg(0) &= ~0x08; tick_ms(10);

  // 10. Phase offset 180: x1 ramp is half a cycle from segment 1.
  for (int s = 0; s < 2; ++s) block.pot[s] = 0.6f;
  slider(1, 7);
  cfg(1) = (cfg(1) & ~0x0300) | 0x0200; tick_ms(10);
  run_blocks(500);
  float d = val[1] - val[0]; if (d < 0) d += 1.0f;
  printf("   offset diff %.3f\n", d);
  check(fabsf(d - 0.5f) < 0.03f, "phase offset 180 degrees");
  cfg(1) &= ~0x0300; tick_ms(10);

  // 11. Unlink: segment 3 alone uses its own (empty) gate, keeps tempo.
  press(2); tick_ms(200); press(3); tick_ms(2100); release(3); release(2); tick_ms(50);
  check(!cs.linked(2) && !cs.linked(3) && cs.linked(0) && cs.linked(1), "unlink 3-4: 1-2 stay linked");
  check(fabsf(sg[2].sync_period() - 400.0f) < 1.0f, "unlinked segment keeps the tempo");
  clear_wraps(); slider(2, 7);
  run_blocks(800);
  check(near(wraps[2], 16), "unlinked segment free-runs at that tempo");

  // 12. Back to stock behaviour outside link mode: runs as a ramp.
  check(cs.sync_lfo(0), "still Sync LFO");

  printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
  return fails ? 1 : 0;
}
