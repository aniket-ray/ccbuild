#ifndef CCBUILD_VALIDATORS_H
#define CCBUILD_VALIDATORS_H

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "ccbuild/target.h"

namespace ccbuild::validators {

/// Check whether a file path has a recognized C++ source extension.
///
/// Accepted extensions: .cc, .cpp, .cxx, .c++, .c, .C
///
/// @param path  A file path (need not exist on disk).
/// @return true if the extension indicates a C++ source file.
[[nodiscard]] inline bool is_cpp_source(std::string_view path) {
  const auto dot = path.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const auto ext = path.substr(dot);
  return ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" ||
         ext == ".c" || ext == ".C";
}

/// Verify that every target in the project has a unique name.
///
/// @return nullopt if all names are unique, or an error message.
[[nodiscard]] inline std::optional<std::string> check_duplicate_target_names(
    std::span<const std::unique_ptr<Target>> targets) {
  std::set<std::string_view> names;
  for (const auto& t : targets) {
    if (!names.insert(t->name()).second) {
      return std::string("duplicate target name: '") + std::string(t->name()) +
             "'";
    }
  }
  return std::nullopt;
}

/// Verify that every target has at least one source file.
///
/// @return nullopt if all targets have sources, or an error message.
[[nodiscard]] inline std::optional<std::string> check_targets_have_sources(
    std::span<const std::unique_ptr<Target>> targets) {
  for (const auto& t : targets) {
    if (t->sources().empty()) {
      return std::string("target '") + std::string(t->name()) +
             "' has no sources";
    }
  }
  return std::nullopt;
}

/// Verify that every source file has a recognized C++ extension.
///
/// @return nullopt if all sources have valid extensions, or an error message.
[[nodiscard]] inline std::optional<std::string> check_source_extensions(
    std::span<const std::unique_ptr<Target>> targets) {
  for (const auto& t : targets) {
    for (const auto& src : t->sources()) {
      if (!is_cpp_source(src)) {
        return std::string("target '") + std::string(t->name()) +
               "' has invalid source file: '" + src +
               "' (expected .cc, .cpp, .cxx, .c++, .c, or .C)";
      }
    }
  }
  return std::nullopt;
}

/// Verify that no executable links against another executable.
///
/// @return nullopt if all links are valid, or an error message.
[[nodiscard]] inline std::optional<std::string> check_no_exe_links_exe(
    std::span<const std::unique_ptr<Target>> targets) {
  for (const auto& t : targets) {
    for (const Target& dep : t->link_deps()) {
      if (dep.kind() == TargetKind::Executable) {
        return std::string("target '") + std::string(t->name()) +
               "' links against executable '" + std::string(dep.name()) +
               "' (can only link against libraries)";
      }
    }
  }
  return std::nullopt;
}

/// Verify that the link-dependency graph contains no cycles.
///
/// Uses three-colour depth-first search (white/grey/black).
///
/// @return nullopt if the graph is a DAG, or an error message.
[[nodiscard]] inline std::optional<std::string> check_no_link_cycles(
    std::span<const std::unique_ptr<Target>> targets) {
  enum class Mark { none, in_stack, done };
  std::map<std::string_view, Mark, std::less<>> marks;

  std::function<std::optional<std::string>(const Target&)> visit =
      [&](const Target& t) -> std::optional<std::string> {
    marks[t.name()] = Mark::in_stack;
    for (const Target& dep : t.link_deps()) {
      if (marks[dep.name()] == Mark::in_stack) {
        return std::string("link cycle involving '") + std::string(t.name()) +
               "' and '" + std::string(dep.name()) + "'";
      }
      if (marks[dep.name()] == Mark::none) {
        if (auto err = visit(dep)) {
          return err;
        }
      }
    }
    marks[t.name()] = Mark::done;
    return std::nullopt;
  };

  for (const auto& t : targets) {
    if (marks[t->name()] == Mark::none) {
      if (auto err = visit(*t)) {
        return err;
      }
    }
  }

  return std::nullopt;
}

}  // namespace ccbuild::validators

#endif  // CCBUILD_VALIDATORS_H
