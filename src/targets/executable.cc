#include "ccbuild/executable.h"

namespace ccbuild {
Executable::Executable(std::string_view name, std::vector<std::string> sources)
    : Target(name, std::move(sources)) {}

std::string Executable::output_filename() const {
  std::string result;
  result.reserve(14 + name().size());
  result += ".ccbuild/bin/";
  result += name();
  return result;
}

}  // namespace ccbuild