#include "ccbuild/project.h"

#include <cstdio>
#include <functional>
#include <map>
#include <ranges>
#include <set>

#include "internal/ninja_bridge.h"
#include "internal/validators.h"

namespace ccbuild {

Project::Project(std::string_view name) : name_(name) {}

void Project::set_cxx_standard(int std) {
  standard_ = std;
}

Executable& Project::add_executable(
    std::string_view name, std::initializer_list<std::string> sources) {
  auto target =
      std::make_unique<Executable>(name, std::vector<std::string>(sources));
  auto* ptr = target.get();
  targets_.push_back(std::move(target));
  return *ptr;
}

StaticLibrary& Project::add_library(
    std::string_view name, std::initializer_list<std::string> sources) {
  auto target =
      std::make_unique<StaticLibrary>(name, std::vector<std::string>(sources));
  auto* ptr = target.get();
  targets_.push_back(std::move(target));
  return *ptr;
}

std::optional<std::string> Project::validate() const {
  std::set<std::string_view> names;
  for (const auto& t : targets_) {
    if (!names.insert(t->name()).second) {
      return std::string("duplicate target name: '") + std::string(t->name()) +
             "'";
    }
  }

  for (const auto& t : targets_) {
    if (t->sources().empty()) {
      return std::string("target '") + std::string(t->name()) +
             "' has no sources";
    }
  }

  for (const auto& t : targets_) {
    for (const auto& src : t->sources()) {
      if (!validators::is_cpp_source(src)) {
        return std::string("target '") + std::string(t->name()) +
               "' has invalid source file: '" + src +
               "' (expected .cc, .cpp, .cxx, .c++, .c, or .C)";
      }
    }
  }

  for (const auto& t : targets_) {
    for (const Target& dep : t->link_deps()) {
      if (dep.kind() == TargetKind::Executable) {
        return std::string("target '") + std::string(t->name()) +
               "' links against executable '" + std::string(dep.name()) +
               "' (can only link against libraries)";
      }
    }
  }

  enum class Mark { none, in_stack, done };
  std::map<std::string_view, Mark, std::less<>> marks;

  std::function<std::optional<std::string>(const Target&)> visit =
      [&](const Target& t) -> std::optional<std::string> {
    marks[t.name()] = Mark::in_stack;
    for (const Target& dep : t.link_deps()) {
      if (marks[dep.name()] == Mark::in_stack) {
        return std::string("link cycle involving '") + std::string(t.name()) +
               "' and '" + std::string(dep.name()) + "'";
      }
      if (marks[dep.name()] == Mark::none)
        if (auto err = visit(dep))
          return err;
    }
    marks[t.name()] = Mark::done;
    return std::nullopt;
  };

  for (const auto& t : targets_) {
    if (marks[t->name()] == Mark::none)
      if (auto err = visit(*t))
        return err;
  }

  return std::nullopt;
}

int Project::build(bool dry_run) {
  using enum TargetKind;

  if (auto err = validate()) {
    fprintf(stderr, "ccbuild: error: %s\n", err->c_str());
    return 1;
  }

  printf("ccbuild: project '%s' (C++%d)\n", name_.c_str(), standard_);

  size_t compile_count = 0;
  size_t archive_count = 0;
  size_t link_count = 0;

  for (const auto& t : targets_) {
    const char* kind_str = [&] {
      switch (t->kind()) {
      case Executable:
        return "executable";
      case StaticLibrary:
        return "static library";
      }
      return "unknown";
    }();

    printf("  target '%.*s' [%s] -> %s\n", static_cast<int>(t->name().size()),
           t->name().data(), kind_str, t->output_filename().c_str());

    printf("    sources:");
    for (const auto& src : t->sources())
      printf(" %s", src.c_str());
    printf("\n");

    if (!t->compile_options().empty()) {
      printf("    options:");
      for (const auto& opt : t->compile_options())
        printf(" %s", opt.c_str());
      printf("\n");
    }

    static constexpr int kVisibilityCount = 3;
    const char* include_labels[] = { nullptr, nullptr, nullptr };
    include_labels[static_cast<int>(Visibility::Public)] = "includes (PUBLIC):";
    include_labels[static_cast<int>(Visibility::Private)] =
        "includes (PRIVATE):";
    include_labels[static_cast<int>(Visibility::Interface)] =
        "includes (INTERFACE):";
    for (int v = 0; v < kVisibilityCount; ++v) {
      auto dirs = t->include_dirs(static_cast<Visibility>(v));
      if (!dirs.empty()) {
        printf("    %s", include_labels[v]);
        for (const auto& d : dirs)
          printf(" %s", d.c_str());
        printf("\n");
      }
    }

    if (!t->link_deps().empty()) {
      printf("    links:");
      for (const Target& dep : t->link_deps())
        printf(" %.*s", static_cast<int>(dep.name().size()), dep.name().data());
      printf("\n");
    }

    auto obj_paths =
        t->sources() | std::views::transform([&](std::string_view src) {
          return t->object_path(src);
        });

    size_t src_idx = 0;
    for (const auto& obj : obj_paths) {
      printf("    compile: %s -> %s\n", t->sources()[src_idx].c_str(),
             obj.c_str());
      ++src_idx;
      ++compile_count;
    }

    switch (t->kind()) {
    case StaticLibrary:
      printf("    archive: -> %s\n", t->output_filename().c_str());
      ++archive_count;
      break;
    case Executable:
      printf("    link: -> %s\n", t->output_filename().c_str());
      ++link_count;
      break;
    }
  }

  printf("  plan: %zu compile, %zu archive, %zu link\n", compile_count,
         archive_count, link_count);

  if (dry_run)
    return 0;
  return NinjaBridge::build(*this);
}

}  // namespace ccbuild
