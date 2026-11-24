// Runs every registered test (or those whose name contains argv[1]).

#include <chrono>
#include <cstdio>
#include <cstring>

#include "host/harness.h"

namespace host {
std::vector<TestCase>& Registry() {
  static std::vector<TestCase> r;
  return r;
}
}  // namespace host

int main(int argc, char** argv) {
  int passed = 0, failed = 0;
  for (auto& t : host::Registry()) {
    if (argc > 1 && !std::strstr(t.name, argv[1])) continue;
    auto start = std::chrono::steady_clock::now();
    std::printf("[ RUN  ] %s\n", t.name);
    std::fflush(stdout);
    bool ok = t.fn();
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("[ %s ] %s (%.0f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
    (ok ? passed : failed)++;
  }
  std::printf("\n%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
