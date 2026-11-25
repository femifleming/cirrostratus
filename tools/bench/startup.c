// Minimal Cortex-M4F startup for the QEMU benchmark.
#include <stdint.h>
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
int main(void);
void SysTick_Handler(void);
void Default_Handler(void) { while (1) { } }

void Reset_Handler(void) {
  // Enable the FPU (CP10/CP11 full access).
  *(volatile uint32_t*)0xE000ED88 |= (0xFu << 20);
  __asm volatile ("dsb\n isb");
  uint32_t* s = &_sidata;
  for (uint32_t* d = &_sdata; d < &_edata;) *d++ = *s++;
  for (uint32_t* d = &_sbss; d < &_ebss;) *d++ = 0;
  for (void (**f)(void) = __init_array_start; f < __init_array_end; ++f) (*f)();
  main();
  while (1) { }
}

__attribute__((section(".isr_vector"), used))
void (*const vectors[16])(void) = {
  (void (*)(void))(&_estack), Reset_Handler, Default_Handler, Default_Handler,
  Default_Handler, Default_Handler, Default_Handler, 0, 0, 0, 0,
  Default_Handler, Default_Handler, 0, Default_Handler, SysTick_Handler,
};

void* __dso_handle = 0;
void _init(void) { }
void _fini(void) { }
