#include "ccbuild/target.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace ccbuild {
Target::Target(std::string_view name, std::vector<std::string> sources)
    : name_(name), sources_(std::move(sources)) {}

Target& Target::link(Target& dep) {
  if (dep.kind() == TargetKind::Executable) {
    throw std::invalid_argument("cannot link against executable '" +
                                std::string(dep.name()) + "'");
  }
  link_deps_.emplace_back(&dep);
  return *this;
}

Target& Target::add_compile_options(std::initializer_list<std::string> opts) {
  compile_options_.insert(compile_options_.end(), opts.begin(), opts.end());
  return *this;
}

std::string Target::object_path(std::string_view source) const {
  auto dot = source.rfind('.');
  auto stem = (dot != std::string_view::npos) ? source.substr(0, dot) : source;

  std::string result;
  result.reserve(13 + name_.size() + 1 + stem.size() + 2);
  result += ".ccbuild/obj/";
  result += name_;
  result += '/';
  result += stem;
  result += ".o";
  return result;
}

}  // namespace ccbuild