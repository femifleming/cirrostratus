/* CPU benchmark image for QEMU's netduinoplus2 (STM32F405 model). */
ENTRY(Reset_Handler)
_estack = 0x20020000;
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
  RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 128K
  CCMRAM (xrw): ORIGIN = 0x10000000, LENGTH = 64K
}
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } >FLASH
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } >FLASH
  .ARM.exidx : { *(.ARM.exidx*) } >FLASH
  .init_array : { __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .; } >FLASH
  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } >RAM AT>FLASH
  .bss : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } >RAM
  .ccmdata (NOLOAD) : { *(.ccmdata*) } >CCMRAM
}
