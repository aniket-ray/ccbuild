#include "ccbuild/target.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/layout.h"

namespace ccbuild {

namespace fs = std::filesystem;

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

// -- Object Path ---------------------------------------------------------

std::string Target::object_path(std::string_view source) const {
  // Normalize the source path and make it relative, replacing any remaining
  // ".." components so the result can never escape the target's object
  // directory (e.g. "../lib/x.cc" -> "__/lib/x.o").
  fs::path relative;
  for (const auto& part : fs::path(source).lexically_normal().relative_path()) {
    relative /= (part == "..") ? fs::path("__") : part;
  }
  relative.replace_extension(".o");

  return (fs::path(internal::layout::kObjDir) / name_ / relative)
      .generic_string();
}

// -- Include Directories -------------------------------------------------

std::vector<std::string>& Target::include_dirs_for(Visibility vis) {
  switch (vis) {
  case Visibility::Private:
    return private_include_dirs_;
  case Visibility::Public:
    return public_include_dirs_;
  case Visibility::Interface:
    return interface_include_dirs_;
  }
  throw std::invalid_argument("invalid Visibility value");
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
