#include "ccbuild/static_library.h"

namespace ccbuild {
StaticLibrary::StaticLibrary(std::string_view name,
                             std::vector<std::string> sources)
    : Target(name, std::move(sources)) {}

std::string StaticLibrary::output_filename() const {
  std::string result;
  result.reserve(16 + name().size() + 2);
  result += ".ccbuild/lib/lib";
  result += name();
  result += ".a";
  return result;
}

}  // namespace ccbuild