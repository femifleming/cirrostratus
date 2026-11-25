// fletchfoorth CPU benchmark: instructions per sample for the whole engine
// (six segments) on QEMU's Cortex-M4F model (-icount: one virtual nanosecond
// per instruction; SysTick counts that time at 168 MHz). Stages runs at
// 31250 Hz on a 72 MHz M4: 2304 cycles per sample for everything.
// tools/bench/run.sh

#include <stdint.h>
#include <string.h>

#include "stages/dobbel.h"

using namespace stages::dobbel;

extern "C" {
int __errno;
volatile uint32_t systick_wraps = 0;
void SysTick_Handler(void) { ++systick_wraps; }
}

static void Semihost(int op, const void* arg) {
  register int r0 __asm__("r0") = op;
  register const void* r1 __asm__("r1") = arg;
  __asm__ volatile ("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
}
static void Print(const char* s) { Semihost(0x04, s); }
static void PrintNum(uint32_t v) {
  char buf[12];
  int i = 11;
  buf[i] = 0;
  do { buf[--i] = '0' + v % 10; v /= 10; } while (v);
  Print(&buf[i]);
}
static void Exit() {
  static const uint32_t args[2] = { 0x20026, 0 };
  Semihost(0x18, args);
}

#define SYST_CSR (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018)

static uint64_t Ticks() {
  uint32_t w1, v;
  do { w1 = systick_wraps; v = SYST_CVR; } while (w1 != systick_wraps);
  return (uint64_t)w1 * 0x1000000u + (0xFFFFFFu - v);
}

static Memory memory;
static Engine engine;
static stmlib::GateFlags gates[kNumSegments][kMaxBlockSize];
static float out[kNumSegments][kMaxBlockSize];

static uint32_t Measure(const Config& config) {
  Input in[kNumSegments];
  for (int i = 0; i < kNumSegments; ++i) {
    in[i].slider = 0.55f + 0.05f * i;
    in[i].pot = 0.6f;
    in[i].cv = 0.01f;
    in[i].cv_raw = 0.0f;
    in[i].cv_slider = in[i].slider;
    in[i].patched = true;
    in[i].gate = gates[i];
  }
  engine.Init(&memory);
  const int kBlocks = 2000;
  uint64_t total = 0;
  for (int b = 0; b < kBlocks; ++b) {
    for (int i = 0; i < kNumSegments; ++i) {
      in[i].cv_raw = (b & 1) ? 0.3f : -0.3f;
      for (size_t n = 0; n < kMaxBlockSize; ++n) {
        const int t = (b * 8 + n) % 1000;
        gates[i][n] = (stmlib::GateFlags)(t < 500 ? (stmlib::GATE_FLAG_HIGH | (t == 0 ? stmlib::GATE_FLAG_RISING : 0))
                                                  : (t == 500 ? stmlib::GATE_FLAG_FALLING : 0));
      }
    }
    uint64_t a = Ticks();
    engine.Process(config, in, out, kMaxBlockSize);
    total += Ticks() - a;
  }
  return (uint32_t)(total * 1000ull / 168ull / (kBlocks * kMaxBlockSize));
}

static const char* kTypeNames[TYPE_LAST] = { "random", "chaos", "envelope", "oscillator", "processor" };

int main() {
  SYST_RVR = 0xFFFFFF;
  SYST_CVR = 0;
  SYST_CSR = 7;
  while (SYST_CVR == 0) { }

  Config config;
  uint32_t worst = 0;
  for (int t = 0; t < TYPE_LAST; ++t) {
    for (int f = 0; f < kNumFlavours[t]; ++f) {
      config.Init();
      for (int i = 0; i < kNumSegments; ++i) {
        config.segment[i].type = t;
        config.segment[i].flavour[t] = f;
        config.segment[i].alt = (t == TYPE_OSCILLATOR) ? (1 << t) : 0;
      }
      const uint32_t insns = Measure(config);
      if (insns > worst) worst = insns;
      Print("six x "); Print(kTypeNames[t]); Print(" "); PrintNum(f + 1);
      Print(": "); PrintNum(insns); Print(" insns/sample\n");
    }
  }
  config.Init();  // the default: 4 random, envelope, oscillator
  Print("default layout: "); PrintNum(Measure(config)); Print(" insns/sample\n");
  Print("worst: "); PrintNum(worst); Print(" insns/sample (budget 2304 cycles for everything)\n");
  Exit();
  return 0;
}
