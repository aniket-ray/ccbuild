#ifndef CCBUILD_PROJECT_H
#define CCBUILD_PROJECT_H

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"

namespace ccbuild {

/// Top-level container for a ccbuild project.
///
/// Owns all build targets (Executable, StaticLibrary) and orchestrates
/// validation and the build process.  This is the primary entry point
/// for ccbuild build scripts (build.cc).
///
/// Usage:
/// @code
///   Project p("myproject");
///   p.set_cxx_standard(20);
///   auto& lib = p.add_library("mylib", {"lib.cc"});
///   auto& exe = p.add_executable("myapp", {"main.cc"});
///   exe.link(lib);
///   return p.build();
/// @endcode
class Project {
 public:
  /// Create a new project with the given name.
  explicit Project(std::string_view name);

  /// Set the C++ standard version to compile with (e.g. 17, 20, 23).
  /// Defaults to C++17.
  void set_cxx_standard(int cxx_standard);

  /// Register a new executable target.
  ///
  /// Ownership remains with the Project; the returned reference is valid
  /// for the lifetime of the Project.
  Executable& add_executable(std::string_view name,
                             std::initializer_list<std::string> sources);

  /// Register a new static library target.
  ///
  /// Ownership remains with the Project; the returned reference is valid
  /// for the lifetime of the Project.
  StaticLibrary& add_library(std::string_view name,
                             std::initializer_list<std::string> sources);

  /// Validate the project model and execute the build.
  ///
  /// @param dry_run  If true, validates and prints the build plan without
  ///                 actually compiling or linking.
  /// @return 0 on success, non-zero on validation or build failure.
  [[nodiscard]] int build(bool dry_run = false);

  /// @name Accessors
  /// @{

  [[nodiscard]] std::string_view name() const { return name_; }
  [[nodiscard]] int standard() const { return standard_; }
  [[nodiscard]] std::span<const std::unique_ptr<Target>> targets() const {
    return targets_;
  }

  /// @}

 private:
  friend class NinjaBridge;

  /// Validate the project model, returning an error message on failure.
  /// Checks: unique target names, non-empty sources, valid extensions,
  /// no executable-to-executable links, no link cycles.
  [[nodiscard]] std::optional<std::string> validate() const;

  std::string name_;
  int standard_ = 17;
  std::vector<std::unique_ptr<Target>> targets_;
};

}  // namespace ccbuild

#endif  // CCBUILD_PROJECT_H
