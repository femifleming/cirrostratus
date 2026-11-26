// MONSOON_COLD: compile a function for size (-Os) on the firmware. For code
// that runs once (Init) or rarely, where flash matters more than speed.
// The host build (clang) ignores it.

#ifndef MONSOON_DSP_MODES_COLD_CODE_H_
#define MONSOON_DSP_MODES_COLD_CODE_H_

#if defined(__GNUC__) && !defined(__clang__)
#define MONSOON_COLD __attribute__((optimize("Os")))
#else
#define MONSOON_COLD
#endif

#endif  // MONSOON_DSP_MODES_COLD_CODE_H_
