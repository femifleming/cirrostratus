// Cirrostratus firmware - entry point, Typhoon build.
//
// Hardware: Typhoon (Jak Plugg), a Clouds variant on the Grayscale
// Supercell/Microcell layout: STM32F427 + WM8731. The DSP is the Monsoon
// build's (monsoon/dsp, unchanged); only the panel differs (typhoon/).
// Audio runs at 48 kHz in blocks of 32 frames from the codec DMA interrupt;
// the main loop runs the UI and the modes' background work (FFTs etc).

#include <stm32f4xx_conf.h>

#include "typhoon/cv_scaler.h"
#include "typhoon/drivers/codec.h"
#include "typhoon/drivers/dac.h"
#include "typhoon/drivers/debug_pin.h"
#include "typhoon/drivers/system.h"
#include "monsoon/dsp/engine.h"
#include "monsoon/dsp/memory.h"
#include "typhoon/meter.h"
#include "typhoon/mute.h"
#include "typhoon/settings.h"
#include "typhoon/ui.h"
#include "monsoon/version.h"

// #define PROFILE_INTERRUPT 1

using namespace monsoon;
using namespace stmlib;

Engine engine;
Codec codec;
Dac dac;
CvScaler cv_scaler;
Meter in_meter;
Meter out_meter;
Mute mute_in;
Mute mute_out;
Settings settings;
Ui ui;
Controls controls;

// The two big RAM blocks the engine carves up. No malloc anywhere. Same
// sizes as the Monsoon build (the F427's extra 64 KB of SRAM is unused).
uint8_t block_main[kMainMemoryBytes] __attribute__ ((aligned (8)));
uint8_t block_ccm[kCcmMemoryBytes] __attribute__ ((aligned (8), section (".ccmdata")));

int __errno;

// Kept in the image (main reads it) so the binary carries its version.
const char kVersionString[] = "Cirrostratus " CIRROSTRATUS_VERSION " Typhoon";

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

// Same budget as the Monsoon build (same core and clock): only a block that
// really ran out of time (98 %) makes the next one skip the mode.
static const uint32_t kShedCycles = 168000000u / 48000u * kBlockSize * 98u / 100u;

void FillBuffer(Codec::Frame* input, Codec::Frame* output, size_t n) {
#ifdef PROFILE_INTERRUPT
  TIC
#endif
  const uint32_t t0 = DWT->CYCCNT;
  cv_scaler.Read(&controls);
  // IN MUTE fades the input before the engine (the codec's DMA buffer is
  // ours for this block), OUT MUTE fades the module's final output.
  mute_in.Process(input, n);
  in_meter.Process(input, n);
  engine.Process(controls, reinterpret_cast<const ShortFrame*>(input),
                 reinterpret_cast<ShortFrame*>(output), n);
  mute_out.Process(output, n);
  out_meter.Process(output, n);
  engine.set_shed(DWT->CYCCNT - t0 > kShedCycles);
#ifdef PROFILE_INTERRUPT
  TOC
#endif
}

void Init() {
  System sys;
  sys.Init(true);
  // First thing: PA4 low, so the AUX normal (every unpatched CV jack) sits
  // at 0 V instead of floating.
  dac.Init();
  // No Version::Init() here: on the Monsoon it puts a pull-up on PB0, which
  // is the WET CV input on the Typhoon. The Typhoon's codec is always the
  // I2S master (own 12.288 MHz crystal), the "revised" case.

  settings.Init();
  cv_scaler.Init(settings.mutable_calibration_data());
  memset(&controls, 0, sizeof(controls));
  in_meter.Init(48000);
  out_meter.Init(48000);
  mute_in.Init();
  mute_out.Init();

  bool engine_ok = engine.Init(block_main, sizeof(block_main),
                               block_ccm, sizeof(block_ccm));
  ui.Init(&settings, &cv_scaler, &engine, &in_meter, &out_meter,
          &mute_in, &mute_out, &dac);
  if (!engine_ok) {
    ui.Panic();
  }

  if (!codec.Init(false, 48000)) {
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
