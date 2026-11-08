#ifndef CCBUILD_INTERNAL_COMPILER_H
#define CCBUILD_INTERNAL_COMPILER_H

#include <optional>
#include <string>
#include <string_view>

namespace ccbuild::internal {

/// Identifies the compiler toolchain.
enum class CompilerKind { Gcc, Clang, Unknown };

/// Information about a detected C++ compiler.
struct CompilerInfo {
  /// Path to the compiler binary (e.g. "/usr/bin/g++").
  std::string path;

  /// Which compiler family was detected.
  CompilerKind kind = CompilerKind::Unknown;

  /// Version string (e.g. "13.2.0"), or empty if it could not be parsed.
  std::string version;

  /// Major version number extracted from the version string,
  /// or 0 if it could not be parsed.
  int major_version = 0;

  /// @return "GCC", "Clang", or "Unknown" based on #kind.
  [[nodiscard]] std::string_view kind_str() const;
};

/// Probe a single compiler candidate: resolve it (a bare name is looked up
/// on PATH, never in the current directory) and run `<compiler> --version`.
///
/// @return CompilerInfo if the candidate exists and reports a version
///         banner, std::nullopt otherwise.
[[nodiscard]] std::optional<CompilerInfo> probe_compiler(
    std::string_view name_or_path);

/// Detect the C++ compiler used to build project sources.
///
/// Priority:
///   1. The compiler named by the $CXX environment variable (a warning is
///      printed if $CXX is set but unusable).
///   2. g++ (found via PATH).
///   3. clang++ (found via PATH).
///
/// @return CompilerInfo if a usable compiler was found, std::nullopt otherwise.
[[nodiscard]] std::optional<CompilerInfo> detect_compiler();

/// @name Parsing helpers (exposed for testing)
/// @{

/// Identify the compiler family from its --version output.
[[nodiscard]] CompilerKind identify_kind(std::string_view version_output);

/// Extract the first "X.Y.Z" version from compiler output.
[[nodiscard]] std::string extract_version(const std::string& version_output);

/// Parse the leading integer from a version string.
/// @return The major version, or 0 on parse failure.
[[nodiscard]] int extract_major(const std::string& version);

/// @}

}  // namespace ccbuild::internal

#endif  // CCBUILD_INTERNAL_COMPILER_H
