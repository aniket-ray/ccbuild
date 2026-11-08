#include "ccbuild/static_library.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/layout.h"

namespace ccbuild {

StaticLibrary::StaticLibrary(std::string_view name,
                             std::vector<std::string> sources)
    : Target(name, std::move(sources)) {}

std::string StaticLibrary::output_filename() const {
  std::string result(internal::layout::kLibDir);
  result += "/lib";
  result += name();
  result += ".a";
  return result;
}

}  // namespace ccbuild
