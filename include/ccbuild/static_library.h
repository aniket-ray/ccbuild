#ifndef CCBUILD_STATIC_LIBRARY_H
#define CCBUILD_STATIC_LIBRARY_H

#include "ccbuild/target.h"

namespace ccbuild {
class StaticLibrary final : public Target {
 public:
  StaticLibrary(std::string_view name, std::vector<std::string> sources);

  [[nodiscard]] TargetKind kind() const override {
    return TargetKind::StaticLibrary;
  }

  [[nodiscard]] std::string output_filename() const override;
};
}  // namespace ccbuild

#endif  // CCBUILD_STATIC_LIBRARY_H
