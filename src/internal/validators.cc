#include "internal/validators.h"

#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ccbuild::internal::validators {

namespace {

/// Quote a name for an error message: 'name'.
std::string quoted(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  out += '\'';
  out += s;
  out += '\'';
  return out;
}

bool is_target_name_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '+' || c == '-';
}

}  // namespace

// -- Predicates ----------------------------------------------------------

bool is_cpp_source(std::string_view path) {
  const auto dot = path.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const auto ext = path.substr(dot);
  return ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" ||
         ext == ".c" || ext == ".C";
}

bool is_valid_target_name(std::string_view name) {
  if (name.empty() || name == "." || name == "..") {
    return false;
  }
  return std::ranges::all_of(name, is_target_name_char);
}

bool is_supported_cxx_standard(int standard) {
  static constexpr std::array kSupported = { 98, 11, 14, 17, 20, 23, 26 };
  return std::ranges::find(kSupported, standard) != kSupported.end();
}

// -- Individual checks ---------------------------------------------------

Result check_target_names(Targets targets) {
  for (const auto& t : targets) {
    if (!is_valid_target_name(t->name())) {
      return "invalid target name " + quoted(t->name()) +
             " (use letters, digits, '_', '.', '+' or '-')";
    }
  }
  return std::nullopt;
}

Result check_duplicate_target_names(Targets targets) {
  std::set<std::string_view> names;
  for (const auto& t : targets) {
    if (!names.insert(t->name()).second) {
      return "duplicate target name: " + quoted(t->name());
    }
  }
  return std::nullopt;
}

Result check_cxx_standard(int standard) {
  if (!is_supported_cxx_standard(standard)) {
    return "unsupported C++ standard: " + std::to_string(standard) +
           " (expected 98, 11, 14, 17, 20, 23, or 26)";
  }
  return std::nullopt;
}

Result check_targets_have_sources(Targets targets) {
  for (const auto& t : targets) {
    if (t->sources().empty()) {
      return "target " + quoted(t->name()) + " has no sources";
    }
  }
  return std::nullopt;
}

Result check_source_extensions(Targets targets) {
  for (const auto& t : targets) {
    for (const auto& src : t->sources()) {
      if (!is_cpp_source(src)) {
        return "target " + quoted(t->name()) +
               " has invalid source file: " + quoted(src) +
               " (expected .cc, .cpp, .cxx, .c++, .c, or .C)";
      }
    }
  }
  return std::nullopt;
}

Result check_unique_object_paths(Targets targets) {
  for (const auto& t : targets) {
    // object path -> the first source that produced it
    std::unordered_map<std::string, std::string_view> seen;
    for (const auto& src : t->sources()) {
      auto [it, inserted] = seen.emplace(t->object_path(src), src);
      if (inserted) {
        continue;
      }
      if (it->second == src) {
        return "target " + quoted(t->name()) + " lists source " + quoted(src) +
               " more than once";
      }
      return "target " + quoted(t->name()) + " has sources " +
             quoted(it->second) + " and " + quoted(src) +
             " that both compile to " + quoted(it->first);
    }
  }
  return std::nullopt;
}

Result check_link_deps_in_project(Targets targets) {
  std::unordered_set<const Target*> owned;
  for (const auto& t : targets) {
    owned.insert(t.get());
  }
  for (const auto& t : targets) {
    for (const Target& dep : t->link_deps()) {
      if (!owned.contains(&dep)) {
        return "target " + quoted(t->name()) + " links " + quoted(dep.name()) +
               ", which is not part of this project";
      }
    }
  }
  return std::nullopt;
}

Result check_no_exe_links_exe(Targets targets) {
  for (const auto& t : targets) {
    for (const Target& dep : t->link_deps()) {
      if (dep.kind() == TargetKind::Executable) {
        return "target " + quoted(t->name()) + " links against executable " +
               quoted(dep.name()) + " (can only link against libraries)";
      }
    }
  }
  return std::nullopt;
}

Result check_no_link_cycles(Targets targets) {
  // Three-colour depth-first search.  `path` holds the targets currently
  // on the DFS stack so a back edge can be reported as the full cycle.
  enum class Mark { kUnvisited, kInProgress, kDone };
  std::unordered_map<const Target*, Mark> marks;
  std::vector<const Target*> path;

  std::function<Result(const Target&)> visit = [&](const Target& t) -> Result {
    marks[&t] = Mark::kInProgress;
    path.push_back(&t);
    for (const Target& dep : t.link_deps()) {
      const Mark mark = marks[&dep];
      if (mark == Mark::kInProgress) {
        std::string cycle = "link cycle: ";
        const auto start = std::ranges::find(path, &dep);
        for (auto it = start; it != path.end(); ++it) {
          cycle += (*it)->name();
          cycle += " -> ";
        }
        cycle += dep.name();
        return cycle;
      }
      if (mark == Mark::kUnvisited) {
        if (auto err = visit(dep)) {
          return err;
        }
      }
    }
    path.pop_back();
    marks[&t] = Mark::kDone;
    return std::nullopt;
  };

  for (const auto& t : targets) {
    if (marks[t.get()] == Mark::kUnvisited) {
      if (auto err = visit(*t)) {
        return err;
      }
    }
  }
  return std::nullopt;
}

// -- Aggregate -----------------------------------------------------------

Result check_all(int cxx_standard, Targets targets) {
  // Ordered so that structural problems (bad names, missing sources) are
  // reported before graph problems that may be consequences of them.
  const std::array<std::function<Result()>, 9> checks = {
    [&] { return check_target_names(targets); },
    [&] { return check_duplicate_target_names(targets); },
    [&] { return check_cxx_standard(cxx_standard); },
    [&] { return check_targets_have_sources(targets); },
    [&] { return check_source_extensions(targets); },
    [&] { return check_unique_object_paths(targets); },
    [&] { return check_link_deps_in_project(targets); },
    [&] { return check_no_exe_links_exe(targets); },
    [&] { return check_no_link_cycles(targets); },
  };
  for (const auto& check : checks) {
    if (auto err = check()) {
      return err;
    }
  }
  return std::nullopt;
}

}  // namespace ccbuild::internal::validators
