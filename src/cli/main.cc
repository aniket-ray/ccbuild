#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>

#include "cli/driver.h"

int main(int argc, char* argv[]) {
  try {
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);  // NOLINT(*-pointer-arithmetic)
    }
    return ccbuild::cli::run(args);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "ccbuild: internal error: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "ccbuild: internal error: unknown exception\n");
  }
  return 1;
}
