#include "ccbuild/target.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
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
