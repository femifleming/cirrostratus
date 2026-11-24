// See bal_knob.h.

#include "monsoon/dsp/modes/bal_knob.h"

#include "monsoon/dsp/core.h"

namespace monsoon {

float BalKnob(float k) { return KnobBipolar(k); }

}  // namespace monsoon
