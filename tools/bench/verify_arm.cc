// The verify run (verify.h) on QEMU's STM32F405: every mode and option,
// built with the firmware's flags. Output via ARM semihosting.
//   BENCH_MAIN=verify_arm tools/bench/run.sh   (tools/bench/verify.sh)

#include <stdint.h>
#include <string.h>

#include "monsoon/dsp/memory.h"
#include "tools/bench/verify.h"

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
static void Exit() {
  // SYS_EXIT on 32-bit ARM takes the reason itself (not a pointer):
  // ADP_Stopped_ApplicationExit, so QEMU exits with status 0.
  Semihost(0x18, reinterpret_cast<const void*>(0x20026));
}

struct SemihostOut : verify::Out {
  void Print(const char* s) { Semihost(0x04, s); }
};
static SemihostOut out;

extern "C" void Bench_Fault(void) {
  out.Print("FAULT: the CPU took a fault (hard/bus/usage)\n");
  Exit();
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

#ifndef VERIFY_BLOCKS
#define VERIFY_BLOCKS 1000
#endif

int main() {
  // Trap divide-by-zero and unaligned doubleword access like a strict core.
  *(volatile uint32_t*)0xE000ED14 |= (1u << 4);  // CCR.DIV_0_TRP
  SYST_RVR = 0xFFFFFF;
  SYST_CVR = 0;
  SYST_CSR = 7;
  while (SYST_CVR == 0) { }
  out.Print("verify: ARM build, firmware flags\n");
  verify::RunAll(engine, block_main, sizeof(block_main), block_ccm,
                 sizeof(block_ccm), out, Ticks, VERIFY_BLOCKS);
  Exit();
  return 0;
}
