#ifndef CCBUILD_COMPILER_H
#define CCBUILD_COMPILER_H

#include <optional>
#include <string>
#include <string_view>

namespace ccbuild {

/// Identifies the compiler toolchain.
enum class CompilerKind { Gcc, Clang, Unknown };

/// Information about a detected C++ compiler.
struct CompilerInfo {
  /// Full path to the compiler binary (e.g. "/usr/bin/g++").
  std::string path;

  /// Which compiler family was detected.
  CompilerKind kind = CompilerKind::Unknown;

  /// Human-readable version string (e.g. "13.2.0").
  std::string version;

  /// Major version number extracted from the version string,
  /// or 0 if it could not be parsed.
  int major_version = 0;

  /// @return "GCC", "Clang", or "Unknown" based on #kind.
  [[nodiscard]] std::string_view kind_str() const;
};

/// Detect the system C++ compiler.
///
/// Priority:
///   1. The compiler named by the $CXX environment variable.
///   2. g++ (found via PATH).
///   3. clang++ (found via PATH).
///
/// @return CompilerInfo if a usable compiler was found, std::nullopt otherwise.
[[nodiscard]] std::optional<CompilerInfo> detect_compiler();

/// Implementation helpers used internally and in tests.
namespace internal {

/// Identify the compiler family from its --version output.
[[nodiscard]] CompilerKind identify_kind(std::string_view version_output);

/// Extract a "X.Y.Z" version from compiler output using a regex.
[[nodiscard]] std::string extract_version(const std::string& version_output);

/// Parse the leading integer from a version string.
/// @return The major version, or 0 on parse failure.
[[nodiscard]] int extract_major(const std::string& version);

}  // namespace internal

}  // namespace ccbuild

#endif  // CCBUILD_COMPILER_H
