#ifndef CCBUILD_PROJECT_H
#define CCBUILD_PROJECT_H

#include <memory>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"

namespace ccbuild {
class Project {
 public:
  explicit Project(std::string_view name);
  void set_cxx_standard(int std);

  Executable& add_executable(std::string_view name,
                             std::initializer_list<std::string> sources);

  StaticLibrary& add_library(std::string_view name,
                             std::initializer_list<std::string> sources);

  // validate the model and execute the build
  // if dry_run is true, only validates and print the plan without compiling
  // return 0 on success, non-zero on error
  [[nodiscard]] int build(bool dry_run = false);

  [[nodiscard]] std::string_view name() const { return name_; }
  [[nodiscard]] int standard() const { return standard_; }
  [[nodiscard]] std::span<const std::unique_ptr<Target>> targets() const {
    return targets_;
  }

 private:
  friend class NinjaBridge;

  [[nodiscard]] bool validate(std::string& err) const;

  std::string name_;
  int standard_ = 17;
  std::vector<std::unique_ptr<Target>> targets_;
};
}  // namespace ccbuild

#endif  // CCBUILD_PROJECT_H
