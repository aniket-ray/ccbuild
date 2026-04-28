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

bool Project::validate(std::string& err) const {
  // Check for duplicate target names.
  std::set<std::string_view> names;
  for (const auto& t : targets_) {
    if (!names.insert(t->name()).second) {
      err = "duplicate target name: '";
      err += t->name();
      err += "'";
      return false;
    }
  }

  // Check for empty sources.
  for (const auto& t : targets_) {
    if (t->sources().empty()) {
      err = "target '";
      err += t->name();
      err += "' has no sources";
      return false;
    }
  }

  // Check for invalid source file extensions.
  for (const auto& t : targets_) {
    for (const auto& src : t->sources()) {
      if (!validators::is_cpp_source(src)) {
        err = "target '";
        err += t->name();
        err += "' has invalid source file: '";
        err += src;
        err += "' (expected .cc, .cpp, .cxx, .c++, .c, or .C)";
        return false;
      }
    }
  }

  // check execs only link against libraries, not other execs
  for (const auto& t : targets_) {
    for (const auto* dep : t->link_deps()) {
      if (dep->kind() == TargetKind::Executable) {
        err = "target '";
        err += t->name();
        err += "' links against executable '";
        err += dep->name();
        err += "' (can only link against libraries)";
        return false;
      }
    }
  }

  // Check for link cycles via DFS.
  enum Mark { None, InStack, Done };
  std::map<const Target*, Mark> marks;

  std::function<bool(const Target*)> visit = [&](const Target* t) -> bool {
    marks[t] = InStack;
    for (const Target* dep : t->link_deps()) {
      if (marks[dep] == InStack) {
        err = "link cycle involving '";
        err += t->name();
        err += "' and '";
        err += dep->name();
        err += "'";
        return false;
      }
      if (marks[dep] == None && !visit(dep))
        return false;
    }
    marks[t] = Done;
    return true;
  };

  for (const auto& t : targets_) {
    if (marks[t.get()] == None && !visit(t.get()))
      return false;
  }

  return true;
}

int Project::build(bool dry_run) {
  using enum TargetKind;

  // Validate.
  std::string err;
  if (!validate(err)) {
    fprintf(stderr, "ccbuild: error: %s\n", err.c_str());
    return 1;
  }

  // Print build plan.
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

    // Sources.
    printf("    sources:");
    for (const auto& src : t->sources())
      printf(" %s", src.c_str());
    printf("\n");

    // Compile options.
    if (!t->compile_options().empty()) {
      printf("    options:");
      for (const auto& opt : t->compile_options())
        printf(" %s", opt.c_str());
      printf("\n");
    }

    // Link dependencies.
    if (!t->link_deps().empty()) {
      printf("    links:");
      for (const auto* dep : t->link_deps())
        printf(" %.*s", static_cast<int>(dep->name().size()),
               dep->name().data());
      printf("\n");
    }

    // Compile edges -- use ranges to derive object paths.
    auto obj_paths =
        t->sources() | std::views::transform([&](const std::string& src) {
          return t->object_path(src);
        });

    size_t src_idx = 0;
    for (const auto& obj : obj_paths) {
      printf("    compile: %s -> %s\n", t->sources()[src_idx].c_str(),
             obj.c_str());
      ++src_idx;
      ++compile_count;
    }

    // Final link/archive edge.
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

  // Execute via ninja bridge (skip in dry-run mode).
  if (dry_run)
    return 0;
  return NinjaBridge::build(*this);
}

}  // namespace ccbuild
