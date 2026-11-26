// Cirrostratus firmware - entry point.
//
// Hardware: Monsoon (Jak Plugg), a 12HP Clouds variant: STM32F405 + WM8731.
// Audio runs at 48 kHz in blocks of 32 frames from the codec DMA interrupt;
// the main loop runs the UI and the modes' background work (FFTs etc).

#include <stm32f4xx_conf.h>

#include "monsoon/cv_scaler.h"
#include "monsoon/drivers/codec.h"
#include "monsoon/drivers/debug_pin.h"
#include "monsoon/drivers/system.h"
#include "monsoon/drivers/version.h"
#include "monsoon/dsp/engine.h"
#include "monsoon/dsp/memory.h"
#include "monsoon/meter.h"
#include "monsoon/settings.h"
#include "monsoon/ui.h"
#include "monsoon/version.h"

// #define PROFILE_INTERRUPT 1

using namespace monsoon;
using namespace stmlib;

Engine engine;
Codec codec;
CvScaler cv_scaler;
Meter meter;
Settings settings;
Ui ui;
Controls controls;

// The two big RAM blocks the engine carves up. No malloc anywhere.
uint8_t block_main[kMainMemoryBytes] __attribute__ ((aligned (8)));
uint8_t block_ccm[kCcmMemoryBytes] __attribute__ ((aligned (8), section (".ccmdata")));

int __errno;

// Kept in the image (main reads it) so the binary carries its version.
const char kVersionString[] = "Cirrostratus " CIRROSTRATUS_VERSION;

extern "C" {

void NMI_Handler() { }
void HardFault_Handler() { while (1); }
void MemManage_Handler() { while (1); }
void BusFault_Handler() { while (1); }
void UsageFault_Handler() { while (1); }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }

void SysTick_Handler() {
  ui.Poll();
}

}

// The audio interrupt's share of a block (32 frames at 48 kHz = 112,000
// cycles at 168 MHz): only a block that really ran out of time (98 %) makes
// the next one skip the mode (see Engine::set_shed), so the panel and mode
// changes always get their time. (1.3-1.5 shed past 85 %: the heavier modes
// crossed that now and then on the module, and every skipped block was a
// click.)
static const uint32_t kShedCycles = 168000000u / 48000u * kBlockSize * 98u / 100u;

void FillBuffer(Codec::Frame* input, Codec::Frame* output, size_t n) {
#ifdef PROFILE_INTERRUPT
  TIC
#endif
  const uint32_t t0 = DWT->CYCCNT;
  cv_scaler.Read(&controls);
  engine.Process(controls, reinterpret_cast<const ShortFrame*>(input),
                 reinterpret_cast<ShortFrame*>(output), n);
  meter.Process(controls.freeze ? output : input, n);
  engine.set_shed(DWT->CYCCNT - t0 > kShedCycles);
#ifdef PROFILE_INTERRUPT
  TOC
#endif
}

void Init() {
  System sys;
  Version version;
  sys.Init(true);
  version.Init();

  settings.Init();
  cv_scaler.Init(settings.mutable_calibration_data());
  memset(&controls, 0, sizeof(controls));
  meter.Init(48000);

  bool engine_ok = engine.Init(block_main, sizeof(block_main),
                               block_ccm, sizeof(block_ccm));
  ui.Init(&settings, &cv_scaler, &engine, &meter);
  if (!engine_ok) {
    ui.Panic();
  }

  bool master = !version.revised();
  if (!codec.Init(master, 48000)) {
    ui.Panic();
  }
  // the cycle counter FillBuffer times itself with
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  if (!codec.Start(kBlockSize, &FillBuffer)) {
    ui.Panic();
  }
#ifdef PROFILE_INTERRUPT
  DebugPin::Init();
#endif
  sys.StartTimers();
}

// The startup code doesn't run C++ static constructors, and every mode is
// a static object with a vtable, so run them here before anything else.
extern "C" {
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
}

static void RunStaticConstructors() {
  for (void (**f)(void) = __init_array_start; f < __init_array_end; ++f) {
    (*f)();
  }
}

int main(void) {
  // Take the version string's address so --gc-sections keeps it.
  const char* volatile version_string = kVersionString;
  (void)version_string;
  RunStaticConstructors();
  Init();
  while (1) {
    ui.DoEvents();
    engine.Prepare();
  }
}
