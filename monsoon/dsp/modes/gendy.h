// STUB - replaced by the real implementation.
#ifndef MONSOON_DSP_MODES_GENDY_H_
#define MONSOON_DSP_MODES_GENDY_H_

#include "monsoon/dsp/mode.h"

namespace monsoon {

class GendyMode : public Mode {
 public:
  const char* name() const { return "gendy"; }
  bool Init(Arena* arena) { (void)arena; return true; }
  void Process(const Controls& c, const FloatFrame* in, FloatFrame* out,
               size_t n) {
    (void)c;
    for (size_t i = 0; i < n; ++i) out[i] = in[i];
  }
};

}  // namespace monsoon

#endif  // MONSOON_DSP_MODES_GENDY_H_
