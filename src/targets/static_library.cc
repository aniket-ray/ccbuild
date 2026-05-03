#include "ccbuild/static_library.h"

namespace ccbuild {
StaticLibrary::StaticLibrary(std::string_view name,
                             std::vector<std::string> sources)
    : Target(name, std::move(sources)) {}

std::string StaticLibrary::output_filename() const {
  static constexpr std::string_view kLibPrefix = ".ccbuild/lib/lib";
  static constexpr std::string_view kLibSuffix = ".a";

  std::string result;
  result.reserve(kLibPrefix.size() + name().size() + kLibSuffix.size());
  result += kLibPrefix;
  result += name();
  result += kLibSuffix;
  return result;
}

}  // namespace ccbuild