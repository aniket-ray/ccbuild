#ifndef CCBUILD_VALIDATORS_H
#define CCBUILD_VALIDATORS_H

#include <string_view>

namespace ccbuild::validators {
[[nodiscard]] inline bool is_cpp_source(std::string_view path) {
  auto dot = path.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  auto ext = path.substr(dot);
  return ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" ||
         ext == ".c" || ext == ".C";
}

}  // namespace ccbuild::validators

#endif  // CCBUILD_VALIDATORS_H
