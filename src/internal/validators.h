#ifndef CCBUILD_INTERNAL_VALIDATORS_H
#define CCBUILD_INTERNAL_VALIDATORS_H

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "ccbuild/target.h"

/// Checks run on a Project before any build work starts.
///
/// Each check_* function returns std::nullopt when the project passes the
/// check, or a human-readable error message (without the "ccbuild: error:"
/// prefix) describing the first problem found.
namespace ccbuild::internal::validators {

/// The targets of a project, as stored by ccbuild::Project.
using Targets = std::span<const std::unique_ptr<Target>>;

/// nullopt on success, otherwise an error message.
using Result = std::optional<std::string>;

/// @name Predicates
/// @{

/// Whether a path has a recognized C++ source extension.
/// Accepted extensions: .cc, .cpp, .cxx, .c++, .c, .C
[[nodiscard]] bool is_cpp_source(std::string_view path);

/// Whether a target name is valid: one or more of [A-Za-z0-9_.+-] (the
/// same rule CMake applies), and not "." or "..".  Names become file names
/// under .ccbuild/, so path separators and whitespace are rejected.
[[nodiscard]] bool is_valid_target_name(std::string_view name);

/// Whether `standard` is a C++ standard ccbuild can pass to the compiler
/// as -std=c++<standard>: 98, 11, 14, 17, 20, 23 or 26.
[[nodiscard]] bool is_supported_cxx_standard(int standard);

/// @}

/// @name Individual checks
/// @{

/// Every target name satisfies is_valid_target_name().
[[nodiscard]] Result check_target_names(Targets targets);

/// No two targets share a name.
[[nodiscard]] Result check_duplicate_target_names(Targets targets);

/// The project's C++ standard is supported.
[[nodiscard]] Result check_cxx_standard(int standard);

/// Every target has at least one source file.
[[nodiscard]] Result check_targets_have_sources(Targets targets);

/// Every source file has a recognized C++ extension.
[[nodiscard]] Result check_source_extensions(Targets targets);

/// Within each target, no two sources map to the same object file
/// (e.g. a source listed twice, or "a.cc" next to "a.cpp").
[[nodiscard]] Result check_unique_object_paths(Targets targets);

/// Every link dependency is a target owned by the same project.
[[nodiscard]] Result check_link_deps_in_project(Targets targets);

/// No target links against an executable.
[[nodiscard]] Result check_no_exe_links_exe(Targets targets);

/// The link-dependency graph is acyclic.  The error names the cycle,
/// e.g. "link cycle: a -> b -> a".
[[nodiscard]] Result check_no_link_cycles(Targets targets);

/// @}

/// Run every check above, in a fixed order, and return the first error.
/// Later checks may assume earlier ones passed (e.g. the cycle check relies
/// on all link dependencies belonging to the project).
[[nodiscard]] Result check_all(int cxx_standard, Targets targets);

}  // namespace ccbuild::internal::validators

#endif  // CCBUILD_INTERNAL_VALIDATORS_H
