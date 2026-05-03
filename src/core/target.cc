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
  link_deps_.emplace_back(dep);
  return *this;
}

Target& Target::add_compile_options(std::initializer_list<std::string> opts) {
  compile_options_.insert(compile_options_.end(), opts.begin(), opts.end());
  return *this;
}

std::string Target::object_path(std::string_view source) const {
  auto dot = source.rfind('.');
  auto stem = (dot != std::string_view::npos) ? source.substr(0, dot) : source;

  static constexpr std::string_view kObjPrefix = ".ccbuild/obj/";
  static constexpr std::string_view kObjSuffix = ".o";

  std::string result;
  result.reserve(kObjPrefix.size() + name_.size() + 1 + stem.size() +
                 kObjSuffix.size());
  result += kObjPrefix;
  result += name_;
  result += '/';
  result += stem;
  result += kObjSuffix;
  return result;
}

Target& Target::add_include_dirs(std::initializer_list<std::string> dirs,
                                 Visibility vis) {
  auto& target = [&]() -> std::vector<std::string>& {
    switch (vis) {
    case Visibility::Private:
      return private_include_dirs_;
    case Visibility::Public:
      return public_include_dirs_;
    case Visibility::Interface:
      return interface_include_dirs_;
    }
    return public_include_dirs_;
  }();
  target.insert(target.end(), dirs.begin(), dirs.end());
  return *this;
}

std::span<const std::string> Target::include_dirs(Visibility vis) const {
  switch (vis) {
  case Visibility::Private:
    return private_include_dirs_;
  case Visibility::Public:
    return public_include_dirs_;
  case Visibility::Interface:
    return interface_include_dirs_;
  }
  return {};
}

}  // namespace ccbuild