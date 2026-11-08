#ifndef CCBUILD_PROJECT_H
#define CCBUILD_PROJECT_H

#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"
#include "ccbuild/types.h"

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

  /// Set the C++ standard version to compile with.
  ///
  /// Accepted values: 98, 11, 14, 17, 20, 23, 26.  Defaults to 17.  Any
  /// other value is reported as a validation error when the project is built.
  /// The compiler must also support the chosen standard (e.g. C++26 needs
  /// a recent GCC or Clang).
  void set_cxx_standard(int cxx_standard);

  /// Register a new executable target.
  ///
  /// Ownership remains with the Project; the returned reference is valid
  /// for the lifetime of the Project.
  Executable& add_executable(std::string_view name,
                             std::initializer_list<std::string> sources) {
    return emplace_executable(name, std::vector<std::string>(sources));
  }

  /// @copydoc
  /// add_executable(std::string_view,std::initializer_list<std::string>)
  template <StringRange R>
  Executable& add_executable(std::string_view name, R&& sources) {
    return emplace_executable(name, to_vector(std::forward<R>(sources)));
  }

  /// Register a new static library target.
  ///
  /// Ownership remains with the Project; the returned reference is valid
  /// for the lifetime of the Project.
  StaticLibrary& add_library(std::string_view name,
                             std::initializer_list<std::string> sources) {
    return emplace_library(name, std::vector<std::string>(sources));
  }

  /// @copydoc add_library(std::string_view,std::initializer_list<std::string>)
  template <StringRange R>
  StaticLibrary& add_library(std::string_view name, R&& sources) {
    return emplace_library(name, to_vector(std::forward<R>(sources)));
  }

  /// Validate the project model and execute the build.
  ///
  /// Prints the build plan, then (unless @p dry_run is set) compiles and
  /// links every out-of-date target.
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
  /// Convert any StringRange into a vector of strings (moving the vector
  /// itself when the caller passes a std::vector<std::string> rvalue).
  template <StringRange R>
  static std::vector<std::string> to_vector(R&& range) {
    if constexpr (std::is_same_v<std::remove_cvref_t<R>,
                                 std::vector<std::string>>) {
      return std::vector<std::string>(std::forward<R>(range));
    } else {
      std::vector<std::string> out;
      for (auto&& item : std::forward<R>(range)) {
        out.emplace_back(std::forward<decltype(item)>(item));
      }
      return out;
    }
  }

  /// The single point where targets are created and adopted.
  Executable& emplace_executable(std::string_view name,
                                 std::vector<std::string> sources);
  StaticLibrary& emplace_library(std::string_view name,
                                 std::vector<std::string> sources);

  /// Validate the project model, returning an error message on failure.
  /// See internal/validators.h for the full list of checks.
  [[nodiscard]] std::optional<std::string> validate() const;

  std::string name_;
  int standard_ = 17;
  std::vector<std::unique_ptr<Target>> targets_;
};

}  // namespace ccbuild

#endif  // CCBUILD_PROJECT_H
