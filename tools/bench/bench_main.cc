// Measures instructions per stereo frame for every mode on QEMU's STM32F405
// model (-icount gives one virtual nanosecond per instruction; SysTick runs
// at 168 MHz of that virtual time). Output via ARM semihosting.

#include <stdint.h>
#include <string.h>

#include "monsoon/dsp/engine.h"
#include "monsoon/dsp/memory.h"
#include "monsoon/dsp/stft.h"
#include "monsoon/dsp/modes/chloe.h"
#include "monsoon/dsp/modes/grains.h"

using namespace monsoon;

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
  static const uint32_t args[2] = { 0x20026, 0 };  // ADP_Stopped_ApplicationExit
  Semihost(0x18, args);
}

#define SYST_CSR (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018)

static uint64_t Ticks() {
  uint32_t w1, v;
  do {
    w1 = systick_wraps;
    v = SYST_CVR;
  } while (w1 != systick_wraps);
  return (uint64_t)w1 * 0x1000000u + (0xFFFFFFu - v);
}

Engine engine;
uint8_t block_main[kMainMemoryBytes] __attribute__((aligned(8)));
uint8_t block_ccm[kCcmMemoryBytes] __attribute__((aligned(8), section(".ccmdata")));

static uint32_t rng = 1;
static int16_t Noise() { rng = rng * 1664525u + 1013904223u; return (int16_t)(rng >> 18); }

int main() {
  SYST_RVR = 0xFFFFFF;
  SYST_CVR = 0;
  SYST_CSR = 7;  // core clock, interrupt, enable
  while (SYST_CVR == 0) { }  // wait for the first reload

  // Calibrate: a loop of known length (4 instructions per iteration).
  uint64_t t0 = Ticks();
  for (volatile uint32_t i = 0; i < 2000000; ) { i = i + 1; }
  uint64_t cal = Ticks() - t0;

  Print("calibration: instructions per loop iteration x100 = ");
  PrintNum((uint32_t)(cal * 100000ull / 168ull / 2000000ull)); Print("\n");

  if (!engine.Init(block_main, sizeof(block_main), block_ccm, sizeof(block_ccm))) {
    Print("engine init failed\n");
    Exit();
  }
  ShortFrame in[kBlockSize], out[kBlockSize];
  {
    // Component micro-benchmarks (instructions per frame).
    static Fdn fdn;
    Arena a; a.Init(block_main, sizeof(block_main), 0, 0);
    fdn.Init(&a);
    static ResonanceTamer tamer; tamer.Init();
    static MonoBass mono_bass; mono_bass.Init(60.0f);
    static Limiter lim; lim.Init();
    FloatFrame f[kBlockSize];
    for (size_t i = 0; i < kBlockSize; ++i) { f[i].l = Noise() / 32768.0f; f[i].r = f[i].l * 0.5f; }
    const int N = 2000;
    uint64_t t = Ticks();
    for (int k = 0; k < N; ++k) fdn.Process(0.5f, false, f, f, kBlockSize);
    Print("fdn "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / (N * kBlockSize))); Print("\n");
    t = Ticks();
    float acc = 0;
    for (int k = 0; k < N; ++k) for (size_t i = 0; i < kBlockSize; ++i) acc += tamer.Process(f[i].l, 0.8f);
    Print("tamer "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / (N * kBlockSize))); Print("\n");
    t = Ticks();
    for (int k = 0; k < N; ++k) { mono_bass.Process(f, kBlockSize); acc += f[0].l; }
    Print("mono bass "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / (N * kBlockSize))); Print("\n");
    t = Ticks();
    for (int k = 0; k < N; ++k) for (size_t i = 0; i < kBlockSize; ++i) { float l = f[i].l, r = f[i].r; lim.Process(&l, &r); acc += l; }
    Print("limiter "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / (N * kBlockSize))); Print("\n");
    t = Ticks();
    for (int k = 0; k < N; ++k) for (size_t i = 0; i < kBlockSize; ++i) { acc += FastSin(f[i].l) + FastCos(f[i].r); }
    Print("sin+cos "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / (N * kBlockSize))); Print("\n");
    {
      float* fa = (float*)block_main;
      float* fb = fa + 2048;
      for (int i = 0; i < 2048; ++i) fa[i] = Noise() / 32768.0f;
      t = Ticks();
      for (int k = 0; k < 20; ++k) { FftDirect(fa, fb, 2048); FftInverse(fb, fa, 2048); }
      Print("fft2048 fwd+inv "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / 20)); Print("\n");
      t = Ticks();
      for (int k = 0; k < 20; ++k) { FftDirect(fa, fb, 512); FftInverse(fb, fa, 512); }
      Print("fft512 fwd+inv "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / 20)); Print("\n");
      t = Ticks();
      for (int k = 0; k < 20000; ++k) acc += FastAtan2Cycles(fa[k & 1023], fa[(k + 7) & 1023]);
      Print("atan2 "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / 20000)); Print("\n");
      t = Ticks();
      for (int k = 0; k < 20000; ++k) acc += sqrtf(fabsf(fa[k & 1023]));
      Print("sqrtf "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / 20000)); Print("\n");
    }
    {
      using namespace monsoon::chloe;
      Arena ca; ca.Init(block_main, sizeof(block_main), 0, 0);
      Tube tube; tube.Init();
      MultiModeFilter filt;
      Pt2399Line* pt = ca.Allocate<Pt2399Line>(1); pt->Init(&ca, 1); pt->SetTime(0.278f);
      const int M = 20000;
      t = Ticks();
      for (int k = 0; k < M; ++k) acc += tube.Process(Noise() / 32768.0f, 0.6f, 18.0f, 0.3f, 0.2f, 0.9f, true);
      Print("chloe tube/sample "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / M)); Print("\n");
      t = Ticks();
      for (int k = 0; k < M; ++k) acc += filt.Process(Noise() / 32768.0f, 0.1f, 1.0f, 0.4f);
      Print("chloe filter/sample "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / M)); Print("\n");
      t = Ticks();
      for (int k = 0; k < M; ++k) acc += pt->Process(Noise() / 32768.0f, false);
      Print("chloe pt2399/sample "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / M)); Print("\n");
      pt->SetTime(1.5f); for (int k = 0; k < 48000; ++k) acc += pt->Process(0.0f, false);
      t = Ticks();
      for (int k = 0; k < M; ++k) acc += pt->Process(Noise() / 32768.0f, false);
      Print("chloe pt2399 at 1.5 s/sample "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / M)); Print("\n");
      t = Ticks();
      for (int k = 0; k < M; ++k) acc += Noise();
      Print("noise gen/sample "); PrintNum((uint32_t)((Ticks() - t) * 1000ull / 168ull / M)); Print("\n");
    }
    if (acc == 12345.0f) Print("x");
  }
  // The micro-benchmarks above reuse block_main as scratch, which trashes the
  // mode Engine::Init brought up (GRAINS): bring the engine up again.
  engine.Init(block_main, sizeof(block_main), block_ccm, sizeof(block_ccm));
  Controls c;
  memset(&c, 0, sizeof(c));
  const int kBlocks = 300;  // 1 s of audio
#ifdef BENCH_QUALITY
  // BENCH_DEFS="-DBENCH_QUALITY=5" tools/bench/run.sh: the buffer modes at
  // one buffer quality.
  engine.set_buffer_quality(BENCH_QUALITY);
#endif
#ifdef BENCH_FFT
  // BENCH_DEFS="-DBENCH_FFT=1" tools/bench/run.sh: the spectral modes at
  // one FFT setting (1 256, 2 512, 3 1024, 4 2048).
  engine.set_fft_setting(BENCH_FFT);
#endif
  for (int scenario = 0; scenario < 2; ++scenario) {
    for (int m = 0; m < MODE_LAST; ++m) {
#ifdef BENCH_ONLY
      if (m != BENCH_ONLY) continue;  // BENCH_DEFS="-DBENCH_ONLY=1": one mode
#endif
#ifdef BENCH_QUALITY
      if (!engine.mode_instance((ModeId)m)->uses_buffer_quality()) continue;
#endif
#ifdef BENCH_FFT
      if (!engine.mode_instance((ModeId)m)->uses_fft_size()) continue;
#endif
#ifndef BENCH_OPTION
#define BENCH_OPTION 0
#endif
      // BENCH_DEFS="-DBENCH_OPTION=1": every mode at its 2nd option (modes
      // with one option run that one); SPECTRAL's 2nd is SMEAR.
      engine.RequestMode((ModeId)m, BENCH_OPTION);
      memset(&c, 0, sizeof(c));
      for (int k = 0; k < 400 && engine.switching(); ++k) {
        engine.Process(c, in, out, kBlockSize);
        engine.Prepare();
      }
      c.position = 0.4f; c.size = 0.6f; c.density = 0.6f; c.texture = 0.6f;
      c.pitch = 7.0f; c.blend = 0.7f; c.bal = 0.6f; c.fbk = 0.4f;
      c.rvb = 0.5f;
      if (scenario == 1) {  // heavy: everything up
        c.density = 1.0f; c.size = 0.95f; c.texture = 0.9f; c.fbk = 0.9f;
        c.rvb = 0.9f; c.pitch = -12.0f;
      }
      // Warm up so buffers fill (BENCH_WARMUP blocks; 250 = the table).
#ifndef BENCH_WARMUP
#define BENCH_WARMUP 250
#endif
      for (int b = 0; b < BENCH_WARMUP; ++b) {
        for (size_t i = 0; i < kBlockSize; ++i) { in[i].l = Noise(); in[i].r = Noise(); }
        engine.Process(c, in, out, kBlockSize);
        for (int k = 0; k < 4; ++k) engine.Prepare();
      }
      uint64_t process = 0, prepare = 0, peak_block = 0;
      for (int b = 0; b < kBlocks; ++b) {
        for (size_t i = 0; i < kBlockSize; ++i) { in[i].l = Noise(); in[i].r = Noise(); }
        c.trigger = (b % 300) == 0;
        c.gate = (b % 300) < 150;
#ifdef BENCH_JITTER
        // BENCH_DEFS="-DBENCH_JITTER" tools/bench/run.sh: the controls as a
        // real module reads them, never quite still (ADC noise after the
        // smoothing: ~0.001 on the pots, ~0.03 semitone on PITCH via V/OCT)
        {
          const float j = (b & 1) ? 0.001f : -0.001f;
          c.position += j; c.size += j; c.density += j; c.texture += j;
          c.bal += j; c.fbk += j; c.rvb += j; c.blend += j;
          c.pitch += (b & 1) ? 0.03f : -0.03f;
        }
#endif
        c.option = BENCH_OPTION;
        uint64_t a = Ticks();
        engine.Process(c, in, out, kBlockSize);
        uint64_t bb = Ticks();
        for (int k = 0; k < 4; ++k) engine.Prepare();
        uint64_t cc = Ticks();
        process += bb - a;
        prepare += cc - bb;
        if (bb - a > peak_block) peak_block = bb - a;
      }
      // ticks -> instructions using the calibration (4 insns/iteration).
      uint64_t frames = (uint64_t)kBlocks * kBlockSize;
      uint32_t per_frame_x10 = (uint32_t)((process + prepare) * 10000ull / 168ull / frames);
      uint32_t isr_peak = (uint32_t)(peak_block * 1000ull / 168ull / kBlockSize);
#ifdef BENCH_GRAINS
      if (m == MODE_GRAINS) { Print("active grains "); PrintNum((uint32_t) static_cast<GrainsMode*>(engine.mode_instance((ModeId)m))->active_grains()); Print("\n"); }
#endif
      Print(scenario ? "heavy   " : "typical ");
      Print(Engine::mode_name((ModeId)m));
      Print(" insns/frame ");
      PrintNum(per_frame_x10 / 10); Print("."); PrintNum(per_frame_x10 % 10);
      Print("  (isr ");
      PrintNum((uint32_t)(process * 1000ull / 168ull / frames));
      Print(", background ");
      PrintNum((uint32_t)(prepare * 1000ull / 168ull / frames));
      Print(", worst isr block ");
      PrintNum(isr_peak);
      Print("/frame)\n");
    }
  }
  Exit();
  return 0;
}
