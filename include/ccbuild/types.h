#ifndef CCBUILD_TYPES_H
#define CCBUILD_TYPES_H

#include <concepts>
#include <ranges>
#include <string>

namespace ccbuild {

/// Visibility level for include directories.
/// Determines how include paths propagate to dependent targets.
///
///   - Private:  used by this target only, not inherited by dependents.
///   - Public:   used by this target and inherited by direct dependents.
///   - Interface: not used by this target, but inherited by dependents
///     (useful for header-only dependencies).
enum class Visibility { Public, Private, Interface };

/// The kind of build target.
enum class TargetKind { Executable, StaticLibrary };

/// Concept: a range whose elements are convertible to std::string.
/// Used to constrain add_sources() so that any string-like range
/// (vector<string>, list<string>, vector<string_view>, etc.) works.
template <typename R>
concept SourceRange =
    std::ranges::input_range<R> &&
    std::convertible_to<std::ranges::range_value_t<R>, std::string>;

}  // namespace ccbuild

#endif  // CCBUILD_TYPES_H
