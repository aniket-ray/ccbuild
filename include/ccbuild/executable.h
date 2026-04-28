#ifndef CCBUILD_EXECUTABLE_H
#define CCBUILD_EXECUTABLE_H

#include "ccbuild/target.h"

namespace ccbuild {
class Executable final : public Target {
 public:
  Executable(std::string_view name, std::vector<std::string> sources);

  [[nodiscard]] TargetKind kind() const override {
    return TargetKind::Executable;
  }

  [[nodiscard]] std::string output_filename() const override;
};
}  // namespace ccbuild

#endif  // CCBUILD_EXECUTABLE_H
