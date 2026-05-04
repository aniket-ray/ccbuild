#ifndef CCBUILD_STATIC_LIBRARY_H
#define CCBUILD_STATIC_LIBRARY_H

#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/target.h"

namespace ccbuild {

/// A static library build target.
///
/// Produces an archive at .ccbuild/lib/lib<name>.a.  Static libraries
/// may link against other static libraries, transitively propagating
/// their public and interface include directories.
class StaticLibrary final : public Target {
 public:
  /// @param name    Unique name (also the library base name).
  /// @param sources Source files to compile.
  StaticLibrary(std::string_view name, std::vector<std::string> sources);

  [[nodiscard]] TargetKind kind() const override {
    return TargetKind::StaticLibrary;
  }

  /// @return ".ccbuild/lib/lib<name>.a"
  [[nodiscard]] std::string output_filename() const override;
};

}  // namespace ccbuild

#endif  // CCBUILD_STATIC_LIBRARY_H
