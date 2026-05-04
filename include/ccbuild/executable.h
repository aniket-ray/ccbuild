#ifndef CCBUILD_EXECUTABLE_H
#define CCBUILD_EXECUTABLE_H

#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/target.h"

namespace ccbuild {

/// An executable build target.
///
/// Produces a binary at .ccbuild/bin/<name>.  Executables may link
/// against static libraries but not against other executables.
class Executable final : public Target {
 public:
  /// @param name    Unique name (also the binary name).
  /// @param sources Source files to compile.
  Executable(std::string_view name, std::vector<std::string> sources);

  [[nodiscard]] TargetKind kind() const override {
    return TargetKind::Executable;
  }

  /// @return ".ccbuild/bin/<name>"
  [[nodiscard]] std::string output_filename() const override;
};

}  // namespace ccbuild

#endif  // CCBUILD_EXECUTABLE_H
