// The emulator verify run (tools/bench/verify.h) on the Mac, for comparison
// with the ARM build on QEMU: tools/bench/verify.sh runs both.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "monsoon/dsp/memory.h"
#include "tools/bench/verify.h"

struct StdOut : verify::Out {
  void Print(const char* s) { std::fputs(s, stdout); std::fflush(stdout); }
};

static monsoon::Engine engine;

int main(int argc, char** argv) {
  const int blocks = argc > 1 ? std::atoi(argv[1]) : 1000;
  std::vector<uint8_t> main_mem(monsoon::kMainMemoryBytes);
  std::vector<uint8_t> ccm_mem(monsoon::kCcmMemoryBytes);
  StdOut out;
  verify::RunAll(engine, main_mem.data(), main_mem.size(), ccm_mem.data(),
                 ccm_mem.size(), out, NULL, blocks);
  return 0;
}
