// BAL knob for HALL, BLOOM, GAMELAN, MIRROR and POLISH: core.h's
// KnobBipolar (0..1 -> -1..+1, dead zone at noon), compiled once out of
// line. Inlined it costs ~150 bytes of flash per mode; these modes read it
// once per block.

#ifndef MONSOON_DSP_MODES_BAL_KNOB_H_
#define MONSOON_DSP_MODES_BAL_KNOB_H_

namespace monsoon {

float BalKnob(float k);

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_BAL_KNOB_H_
