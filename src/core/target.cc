#include "ccbuild/target.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ccbuild {

// -- Construction --------------------------------------------------------

Target::Target(std::string_view name, std::vector<std::string> sources)
    : name_(name), sources_(std::move(sources)) {}

// -- Link Dependencies ---------------------------------------------------

Target& Target::link(Target& dep) {
  // Only libraries can be linked against -- executables are leaf nodes.
  if (dep.kind() == TargetKind::Executable) {
    throw std::invalid_argument("cannot link against executable '" +
                                std::string(dep.name()) + "'");
  }
  link_deps_.emplace_back(dep);
  return *this;
}

// -- Compile Options -----------------------------------------------------

Target& Target::add_compile_options(std::initializer_list<std::string> opts) {
  compile_options_.insert(compile_options_.end(), opts.begin(), opts.end());
  return *this;
}

// -- Link Options --------------------------------------------------------

Target& Target::add_link_options(std::initializer_list<std::string> opts) {
  link_options_.insert(link_options_.end(), opts.begin(), opts.end());
  return *this;
}

// -- Object Path ---------------------------------------------------------

std::string Target::object_path(std::string_view source) const {
  // Strip the file extension to get the stem.
  const auto dot = source.rfind('.');
  const auto stem =
      (dot != std::string_view::npos) ? source.substr(0, dot) : source;

  static constexpr std::string_view kObjPrefix = ".ccbuild/obj/";
  static constexpr std::string_view kObjSuffix = ".o";

  // Build path: .ccbuild/obj/<target_name>/<stem>.o
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

// -- Include Directories -------------------------------------------------

Target& Target::add_include_dirs(std::initializer_list<std::string> dirs,
                                 Visibility vis) {
  // Select the appropriate include directory vector for the given visibility.
  auto& target = [this, vis]() -> std::vector<std::string>& {
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
