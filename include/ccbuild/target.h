#ifndef CCBUILD_TARGET_H
#define CCBUILD_TARGET_H

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/types.h"

namespace ccbuild {
class NinjaBridge;

class Target {
 public:
  virtual ~Target() = default;

  [[nodiscard]] std::string_view name() const { return name_; }
  [[nodiscard]] virtual TargetKind kind() const = 0;
  [[nodiscard]] virtual std::string output_filename() const = 0;
  [[nodiscard]] std::span<const std::string> sources() const {
    return sources_;
  }

  /// append sources from any range of string-convertible elements
  template <SourceRange R>
  Target& add_sources(R&& range) {
    for (auto&& source : range) {
      sources_.emplace_back(std::forward<decltype(source)>(source));
    }
    return *this;
  }

  /// overload for brace-enclosed initializer lists
  Target& add_sources(std::initializer_list<std::string> sources) {
    sources_.insert(sources_.end(), sources.begin(), sources.end());
    return *this;
  }

  /// link dependencies
  Target& link(Target& dep);
  [[nodiscard]] std::span<Target* const> link_deps() const {
    return link_deps_;
  }

  /// compile options
  Target& add_compile_options(std::initializer_list<std::string> opts);
  [[nodiscard]] std::span<const std::string> compile_options() const {
    return compile_options_;
  }

  [[nodiscard]] std::string object_path(std::string_view source) const;

  /// Non-copyable, non-movable
  Target(const Target&) = delete;
  Target& operator=(const Target&) = delete;

 protected:
  Target(std::string_view name, std::vector<std::string> sources);

 private:
  friend class NinjaBridge;

  std::string name_;
  std::vector<std::string> sources_;
  std::vector<Target*> link_deps_;
  std::vector<std::string> compile_options_;
};

}  // namespace ccbuild

#endif  // CCBUILD_TARGET_H