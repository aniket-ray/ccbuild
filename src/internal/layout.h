#ifndef CCBUILD_INTERNAL_LAYOUT_H
#define CCBUILD_INTERNAL_LAYOUT_H

#include <string_view>

/// On-disk layout of a ccbuild project.
///
/// Every artifact ccbuild produces lives under kBuildDir, relative to the
/// project root (the directory that contains build.cc).  All build-directory
/// paths are defined here and nowhere else.
namespace ccbuild::internal::layout {

/// The build script the CLI looks for in the current directory.
inline constexpr std::string_view kBuildScript = "build.cc";

/// Root of all build output.
inline constexpr std::string_view kBuildDir = ".ccbuild";

/// Object files: <kObjDir>/<target>/<source path>.o
inline constexpr std::string_view kObjDir = ".ccbuild/obj";

/// Linked executables: <kBinDir>/<target>
inline constexpr std::string_view kBinDir = ".ccbuild/bin";

/// Static libraries: <kLibDir>/lib<target>.a
inline constexpr std::string_view kLibDir = ".ccbuild/lib";

/// The compiled build script (cached between runs).
inline constexpr std::string_view kRunnerPath = ".ccbuild/runner";

/// Ninja's build and dependency logs (enable incremental builds).
inline constexpr std::string_view kBuildLogPath = ".ccbuild/.ninja_log";
inline constexpr std::string_view kDepsLogPath = ".ccbuild/.ninja_deps";

}  // namespace ccbuild::internal::layout

#endif  // CCBUILD_INTERNAL_LAYOUT_H
