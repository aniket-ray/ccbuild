#ifndef CCBUILD_TYPES_H
#define CCBUILD_TYPES_H

#include <concepts>
#include <ranges>
#include <string>

namespace ccbuild {
enum class Visibility { Public, Private, Interface };
enum class TargetKind { Executable, StaticLibrary };

template <typename R>
concept SourceRange =
    std::ranges::input_range<R> &&
    std::convertible_to<std::ranges::range_value_t<R>, std::string>;
}  // namespace ccbuild

#endif  // CCBUILD_TYPES_H
