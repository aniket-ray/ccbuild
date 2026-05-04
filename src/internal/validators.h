#ifndef CCBUILD_VALIDATORS_H
#define CCBUILD_VALIDATORS_H

#include <string_view>

namespace ccbuild::validators {

/// Check whether a file path has a recognized C++ source extension.
///
/// Accepted extensions: .cc, .cpp, .cxx, .c++, .c, .C
///
/// @param path  A file path (need not exist on disk).
/// @return true if the extension indicates a C++ source file.
[[nodiscard]] inline bool is_cpp_source(std::string_view path) {
  const auto dot = path.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const auto ext = path.substr(dot);
  return ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" ||
         ext == ".c" || ext == ".C";
}

}  // namespace ccbuild::validators

#endif  // CCBUILD_VALIDATORS_H
