#include "ccbuild/executable.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/layout.h"

namespace ccbuild {

Executable::Executable(std::string_view name, std::vector<std::string> sources)
    : Target(name, std::move(sources)) {}

std::string Executable::output_filename() const {
  std::string result(internal::layout::kBinDir);
  result += '/';
  result += name();
  return result;
}

}  // namespace ccbuild
