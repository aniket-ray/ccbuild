#include "ccbuild/project.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/ninja_bridge.h"
#include "internal/plan_printer.h"
#include "internal/validators.h"

namespace ccbuild {

// -- Construction --------------------------------------------------------

Project::Project(std::string_view name) : name_(name) {}

// -- Configuration -------------------------------------------------------

void Project::set_cxx_standard(int cxx_standard) {
  standard_ = cxx_standard;
}

// -- Target Registration -------------------------------------------------

Executable& Project::emplace_executable(std::string_view name,
                                        std::vector<std::string> sources) {
  auto target = std::make_unique<Executable>(name, std::move(sources));
  Executable& ref = *target;
  targets_.push_back(std::move(target));
  return ref;
}

StaticLibrary& Project::emplace_library(std::string_view name,
                                        std::vector<std::string> sources) {
  auto target = std::make_unique<StaticLibrary>(name, std::move(sources));
  StaticLibrary& ref = *target;
  targets_.push_back(std::move(target));
  return ref;
}

// -- Validation ----------------------------------------------------------

std::optional<std::string> Project::validate() const {
  return internal::validators::check_all(standard_, targets_);
}

// -- Build Execution -----------------------------------------------------

int Project::build(bool dry_run) {
  if (auto err = validate()) {
    std::fprintf(stderr, "ccbuild: error: %s\n", err->c_str());
    return 1;
  }

  internal::print_plan(*this, stdout);
  if (dry_run) {
    return 0;
  }
  std::fflush(stdout);  // keep the plan ahead of Ninja's progress output
  return internal::NinjaBridge::build(*this);
}

}  // namespace ccbuild
