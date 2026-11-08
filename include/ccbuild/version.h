#ifndef CCBUILD_VERSION_H
#define CCBUILD_VERSION_H

#include <string_view>

/// ccbuild's release version (Semantic Versioning).
///
/// This macro is the single source of truth for the version: CMakeLists.txt
/// parses it to set the CMake project version, and the CLI reports it for
/// `ccbuild --version`.
#define CCBUILD_VERSION_STRING "0.1.0"

namespace ccbuild {

/// The ccbuild version, e.g. "0.1.0".
inline constexpr std::string_view kVersion = CCBUILD_VERSION_STRING;

}  // namespace ccbuild

#endif  // CCBUILD_VERSION_H
