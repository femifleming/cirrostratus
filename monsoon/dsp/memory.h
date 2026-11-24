// RAM given to the engine. The firmware allocates exactly these blocks;
// the host harness uses the same numbers so tests see the real limits.

#ifndef MONSOON_DSP_MEMORY_H_
#define MONSOON_DSP_MEMORY_H_

#include <stddef.h>

namespace monsoon {

// Main SRAM (112K + 16K on the F405; the rest holds stack, globals, DMA).
const size_t kMainMemoryBytes = 96 * 1024;
// Core-coupled memory (64K, CPU-only). Reverb lines + mode buffers.
const size_t kCcmMemoryBytes = 64 * 1024 - 64;

}  // namespace monsoon

#endif  // MONSOON_DSP_MEMORY_H_
