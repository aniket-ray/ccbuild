#include "ccbuild/executable.h"

namespace ccbuild {

Executable::Executable(std::string_view name, std::vector<std::string> sources)
    : Target(name, std::move(sources)) {}

std::string Executable::output_filename() const {
  static constexpr std::string_view kBinPrefix = ".ccbuild/bin/";

  std::string result;
  result.reserve(kBinPrefix.size() + name().size());
  result += kBinPrefix;
  result += name();
  return result;
}

}  // namespace ccbuild
