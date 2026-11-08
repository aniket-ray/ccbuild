#include "internal/plan_printer.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "ccbuild/project.h"

namespace ccbuild::internal {
namespace {

void print_text(std::FILE* out, std::string_view text) {
  std::fwrite(text.data(), 1, text.size(), out);
}

/// Print "    <label>: item item ..." if `items` is non-empty.
template <typename Range, typename Proj = std::identity>
void print_list(std::FILE* out, std::string_view label, const Range& items,
                Proj proj = {}) {
  if (std::ranges::empty(items)) {
    return;
  }
  print_text(out, "    ");
  print_text(out, label);
  print_text(out, ":");
  for (const auto& item : items) {
    print_text(out, " ");
    print_text(out, std::invoke(proj, item));
  }
  print_text(out, "\n");
}

std::string_view kind_label(TargetKind kind) {
  switch (kind) {
  case TargetKind::Executable:
    return "executable";
  case TargetKind::StaticLibrary:
    return "static library";
  }
  return "unknown";
}

}  // namespace

void print_plan(const Project& project, std::FILE* out) {
  std::fprintf(out, "ccbuild: project '%.*s' (C++%d)\n",
               static_cast<int>(project.name().size()), project.name().data(),
               project.standard());

  std::size_t compile_count = 0;
  std::size_t archive_count = 0;
  std::size_t link_count = 0;

  static constexpr std::array<std::pair<Visibility, std::string_view>, 3>
      kIncludeLabels = { {
          { Visibility::Public, "includes (PUBLIC)" },
          { Visibility::Private, "includes (PRIVATE)" },
          { Visibility::Interface, "includes (INTERFACE)" },
      } };

  for (const auto& t : project.targets()) {
    const std::string output = t->output_filename();
    std::fprintf(out, "  target '%.*s' [%.*s] -> %s\n",
                 static_cast<int>(t->name().size()), t->name().data(),
                 static_cast<int>(kind_label(t->kind()).size()),
                 kind_label(t->kind()).data(), output.c_str());

    // Every target has sources (validated), so this line always prints.
    print_list(out, "sources", t->sources());
    print_list(out, "options", t->compile_options());
    for (const auto& [vis, label] : kIncludeLabels) {
      print_list(out, label, t->include_dirs(vis));
    }
    print_list(out, "links", t->link_deps(),
               [](const Target& dep) { return dep.name(); });
    print_list(out, "link opts", t->link_options());

    for (const auto& src : t->sources()) {
      std::fprintf(out, "    compile: %s -> %s\n", src.c_str(),
                   t->object_path(src).c_str());
      ++compile_count;
    }

    switch (t->kind()) {
    case TargetKind::StaticLibrary:
      std::fprintf(out, "    archive: -> %s\n", output.c_str());
      ++archive_count;
      break;
    case TargetKind::Executable:
      std::fprintf(out, "    link: -> %s\n", output.c_str());
      ++link_count;
      break;
    }
  }

  std::fprintf(out, "  plan: %zu compile, %zu archive, %zu link\n",
               compile_count, archive_count, link_count);
}

}  // namespace ccbuild::internal
