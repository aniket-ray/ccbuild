#ifndef CCBUILD_TARGET_H
#define CCBUILD_TARGET_H

#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/types.h"

namespace ccbuild {

class NinjaBridge;

/// Abstract base for all build targets (executables, static libraries, etc.).
///
/// Targets own their sources, compile options, link dependencies,
/// and include directories.  The fluent API returns Target& from every
/// mutator so that callers can chain configuration calls.
///
/// Targets are non-copyable and non-movable -- the Project owns them
/// via std::unique_ptr<Target> and exposes them by reference.
class Target {
 public:
  virtual ~Target() = default;

  /// @name Identity
  /// @{

  /// The unique name of this target within the project.
  [[nodiscard]] std::string_view name() const { return name_; }

  /// Which kind of target this is (Executable or StaticLibrary).
  [[nodiscard]] virtual TargetKind kind() const = 0;

  /// The path to the final build artifact (.ccbuild/bin/<name>
  /// or .ccbuild/lib/lib<name>.a).
  [[nodiscard]] virtual std::string output_filename() const = 0;

  /// @}

  /// @name Sources
  /// @{

  /// The source files that make up this target.
  [[nodiscard]] std::span<const std::string> sources() const {
    return sources_;
  }

  /// Append sources from any range of string-convertible elements
  /// (std::vector<std::string>, std::vector<std::string_view>, etc.).
  ///
  /// Constrained by the SourceRange concept (types.h).
  template <SourceRange R>
  Target& add_sources(R&& range) {
    for (auto&& source : range) {
      sources_.emplace_back(std::forward<decltype(source)>(source));
    }
    return *this;
  }

  /// Overload for brace-enclosed initializer lists.
  Target& add_sources(std::initializer_list<std::string> sources) {
    sources_.insert(sources_.end(), sources.begin(), sources.end());
    return *this;
  }

  /// @}

  /// @name Link Dependencies
  /// @{

  /// Add a link dependency on another target.
  ///
  /// Only library targets may be linked against; linking an executable
  /// throws std::invalid_argument.  The dependency is stored by reference --
  /// the caller must ensure the referred target outlives this one.
  Target& link(Target& dep);

  /// All direct link dependencies, in the order they were added.
  [[nodiscard]] std::span<const std::reference_wrapper<Target>> link_deps()
      const {
    return link_deps_;
  }

  /// @}

  /// @name Compile Options
  /// @{

  /// Add compiler flags (e.g. "-Wall", "-O2") that apply to every
  /// source file in this target.
  Target& add_compile_options(std::initializer_list<std::string> opts);

  /// All compile options in the order they were added.
  [[nodiscard]] std::span<const std::string> compile_options() const {
    return compile_options_;
  }

  /// @}

  /// @name Include Directories
  /// @{

  /// Add include directories at the given Visibility level.
  ///
  /// Visibility semantics:
  ///   - Private:   used when compiling this target only.
  ///   - Public:    used when compiling this target and anything that links it.
  ///   - Interface: used only by targets that link this one (not by this target
  ///     itself).  Useful for header-only libraries.
  Target& add_include_dirs(std::initializer_list<std::string> dirs,
                           Visibility vis);

  /// Retrieve include directories for a given visibility level.
  [[nodiscard]] std::span<const std::string> include_dirs(Visibility vis) const;

  /// @}

  /// @name Link Options
  /// @{

  /// Add flags passed to the linker (e.g. "-lpthread", "-ldl").
  Target& add_link_options(std::initializer_list<std::string> opts);

  /// All link options in the order they were added.
  [[nodiscard]] std::span<const std::string> link_options() const {
    return link_options_;
  }

  /// @}

  /// @name Build Artifact Paths
  /// @{

  /// Compute the object file path for a given source file.
  ///
  /// Output: .ccbuild/obj/<target_name>/<stem>.o
  /// (the stem is the source path with its extension stripped).
  [[nodiscard]] std::string object_path(std::string_view source) const;

  /// @}

  /// Targets are non-copyable and non-movable.
  /// The Project owns them via unique_ptr; external code works through
  /// references.
  Target(const Target&) = delete;
  Target& operator=(const Target&) = delete;

 protected:
  /// Construct a target with a name and initial source list.
  /// Called only by derived classes (Executable, StaticLibrary).
  Target(std::string_view name, std::vector<std::string> sources);

 private:
  friend class NinjaBridge;

  std::string name_;
  std::vector<std::string> sources_;
  std::vector<std::reference_wrapper<Target>> link_deps_;
  std::vector<std::string> compile_options_;
  std::vector<std::string> link_options_;
  std::vector<std::string> private_include_dirs_;
  std::vector<std::string> public_include_dirs_;
  std::vector<std::string> interface_include_dirs_;
};

}  // namespace ccbuild

#endif  // CCBUILD_TARGET_H
