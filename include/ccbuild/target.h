#ifndef CCBUILD_TARGET_H
#define CCBUILD_TARGET_H

#include <functional>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ccbuild/types.h"

namespace ccbuild {

/// Abstract base for all build targets (executables, static libraries, etc.).
///
/// Targets own their sources, compile options, link dependencies,
/// and include directories.  The fluent API returns Target& from every
/// mutator so that callers can chain configuration calls.
///
/// Every list-taking mutator accepts either a brace-enclosed list
/// (`{"-Wall", "-O2"}`) or any StringRange (e.g. a std::vector<std::string>
/// computed at runtime).
///
/// Targets are non-copyable and non-movable -- the Project owns them
/// via std::unique_ptr<Target> and exposes them by reference.
class Target {
 public:
  virtual ~Target() = default;

  Target(const Target&) = delete;
  Target& operator=(const Target&) = delete;
  Target(Target&&) = delete;
  Target& operator=(Target&&) = delete;

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

  /// Append source files.
  Target& add_sources(std::initializer_list<std::string> sources) {
    return add_sources(
        std::span<const std::string>(sources.begin(), sources.size()));
  }

  /// @copydoc add_sources(std::initializer_list<std::string>)
  template <StringRange R>
  Target& add_sources(R&& sources) {
    append(sources_, std::forward<R>(sources));
    return *this;
  }

  /// @}

  /// @name Link Dependencies
  /// @{

  /// Add a link dependency on another target.
  ///
  /// Only library targets may be linked against; linking an executable
  /// throws std::invalid_argument.  The dependency is stored by reference --
  /// the caller must ensure the referred target outlives this one, and it
  /// must belong to the same Project (checked when the project is built).
  Target& link(Target& dep);

  /// All direct link dependencies, in the order they were added.
  [[nodiscard]] std::span<const std::reference_wrapper<Target>> link_deps()
      const {
    return link_deps_;
  }

  /// @}

  /// @name Compile Options
  /// @{

  /// Add compiler flags (e.g. "-Wall", "-O2") that apply to every source
  /// file in this target.
  ///
  /// Options are shell syntax: each one is inserted into the compile
  /// command as written and interpreted by /bin/sh.  One element may hold
  /// several flags ("-O2 -g"), command substitution works
  /// ("$(pkg-config --cflags zlib)"), and characters special to the shell
  /// must be quoted for it ("-DGREETING='\"hi there\"'").
  Target& add_compile_options(std::initializer_list<std::string> opts) {
    return add_compile_options(
        std::span<const std::string>(opts.begin(), opts.size()));
  }

  /// @copydoc add_compile_options(std::initializer_list<std::string>)
  template <StringRange R>
  Target& add_compile_options(R&& opts) {
    append(compile_options_, std::forward<R>(opts));
    return *this;
  }

  /// All compile options in the order they were added.
  [[nodiscard]] std::span<const std::string> compile_options() const {
    return compile_options_;
  }

  /// @}

  /// @name Include Directories
  /// @{

  /// Add include directories at the given Visibility level.
  ///
  /// Directories are paths, not shell syntax: each becomes exactly one -I
  /// argument, so names with spaces or other special characters just work.
  ///
  /// Visibility semantics:
  ///   - Private:   used when compiling this target only.
  ///   - Public:    used when compiling this target and anything that links
  ///                it (directly or transitively).
  ///   - Interface: used only by targets that link this one (not by this
  ///                target itself).  Useful for header-only libraries.
  Target& add_include_dirs(std::initializer_list<std::string> dirs,
                           Visibility vis) {
    return add_include_dirs(
        std::span<const std::string>(dirs.begin(), dirs.size()), vis);
  }

  /// @copydoc add_include_dirs(std::initializer_list<std::string>,Visibility)
  template <StringRange R>
  Target& add_include_dirs(R&& dirs, Visibility vis) {
    append(include_dirs_for(vis), std::forward<R>(dirs));
    return *this;
  }

  /// Retrieve include directories for a given visibility level.
  [[nodiscard]] std::span<const std::string> include_dirs(Visibility vis) const;

  /// @}

  /// @name Link Options
  /// @{

  /// Add flags passed to the linker (e.g. "-lpthread", "-ldl").
  ///
  /// Link options are placed after all object files and libraries on the
  /// link command line, so `-l` libraries resolve symbols correctly.  On a
  /// static library they describe what its consumers must link against:
  /// they are applied to every executable that links the library, directly
  /// or transitively (they are never passed to `ar`).  Like compile options
  /// they are shell syntax ("$(pkg-config --libs zlib)" works).
  Target& add_link_options(std::initializer_list<std::string> opts) {
    return add_link_options(
        std::span<const std::string>(opts.begin(), opts.size()));
  }

  /// @copydoc add_link_options(std::initializer_list<std::string>)
  template <StringRange R>
  Target& add_link_options(R&& opts) {
    append(link_options_, std::forward<R>(opts));
    return *this;
  }

  /// All link options in the order they were added.
  [[nodiscard]] std::span<const std::string> link_options() const {
    return link_options_;
  }

  /// @}

  /// @name Build Artifact Paths
  /// @{

  /// Compute the object file path for a given source file.
  ///
  /// Output: .ccbuild/obj/<target_name>/<source path with extension .o>.
  /// The source path is normalized first; `..` components become `__` and
  /// absolute paths are made relative, so the object file always lands
  /// inside this target's object directory.
  [[nodiscard]] std::string object_path(std::string_view source) const;

  /// @}

 protected:
  /// Construct a target with a name and initial source list.
  /// Called only by derived classes (Executable, StaticLibrary).
  Target(std::string_view name, std::vector<std::string> sources);

 private:
  /// Append every element of `range` to `out`, converting to std::string.
  ///
  /// The elements are collected first because `range` may be a view of
  /// `out` itself (e.g. `t.add_sources(t.sources())`), which appending in
  /// place would invalidate.
  template <StringRange R>
  static void append(std::vector<std::string>& out, R&& range) {
    std::vector<std::string> items;
    for (auto&& item : std::forward<R>(range)) {
      items.emplace_back(std::forward<decltype(item)>(item));
    }
    out.insert(out.end(), std::make_move_iterator(items.begin()),
               std::make_move_iterator(items.end()));
  }

  /// The include directory list that stores directories of visibility `vis`.
  std::vector<std::string>& include_dirs_for(Visibility vis);

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
