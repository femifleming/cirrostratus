// Small runtime pieces for a bare-metal C++ build.

#include <stddef.h>
#include <stdint.h>

// Modes have virtual destructors but are never deleted (static objects).
void operator delete(void*) { }
void operator delete(void*, unsigned int) { }

// The Cortex-M4F has no rounding instruction, so newlib's floorf/lrintf are
// slow generic code. These exact versions use one VCVT each and take
// precedence over libm at link time.
extern "C" float floorf(float x) {
  if (!(x > -8388608.0f && x < 8388608.0f)) return x;  // already integral/NaN
  float f = static_cast<float>(static_cast<int32_t>(x));
  return f > x ? f - 1.0f : f;
}

extern "C" long lrintf(float x) {
  float r = floorf(x + 0.5f);
  return static_cast<long>(r);
}
