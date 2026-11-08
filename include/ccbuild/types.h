#ifndef CCBUILD_TYPES_H
#define CCBUILD_TYPES_H

#include <concepts>
#include <ranges>
#include <string>

namespace ccbuild {

/// Visibility level for include directories.
/// Determines how include paths propagate to dependent targets.
///
///   - Private:   used by this target only, not inherited by dependents.
///   - Public:    used by this target and inherited by every target that
///                links it, directly or transitively.
///   - Interface: not used by this target, but inherited by dependents
///                (useful for header-only dependencies).
enum class Visibility { Public, Private, Interface };

/// The kind of build target.
enum class TargetKind { Executable, StaticLibrary };

/// Concept: a range whose elements can be used to construct a std::string.
///
/// Satisfied by std::vector<std::string>, std::vector<std::string_view>,
/// std::array<const char*, N>, std::list<std::string>, views over any of
/// these, and so on.  Used by every API that accepts a list of strings
/// (sources, compile options, include directories, link options).
template <typename R>
concept StringRange =
    std::ranges::input_range<R> &&
    std::constructible_from<std::string, std::ranges::range_reference_t<R>>;

/// Backwards-compatible name for StringRange.
template <typename R>
concept SourceRange = StringRange<R>;

}  // namespace ccbuild

#endif  // CCBUILD_TYPES_H
