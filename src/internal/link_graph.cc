#include "internal/link_graph.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ccbuild::internal {

std::vector<std::string> compile_include_dirs(const Target& target) {
  std::vector<std::string> dirs;
  std::unordered_set<std::string_view> seen;
  auto add = [&](std::span<const std::string> more) {
    for (const auto& dir : more) {
      if (seen.insert(dir).second) {
        dirs.push_back(dir);
      }
    }
  };

  add(target.include_dirs(Visibility::Private));
  add(target.include_dirs(Visibility::Public));

  std::unordered_set<const Target*> visited;
  std::function<void(const Target&)> collect = [&](const Target& t) {
    if (!visited.insert(&t).second) {
      return;
    }
    add(t.include_dirs(Visibility::Public));
    add(t.include_dirs(Visibility::Interface));
    for (const Target& dep : t.link_deps()) {
      collect(dep);
    }
  };
  for (const Target& dep : target.link_deps()) {
    collect(dep);
  }
  return dirs;
}

}  // namespace ccbuild::internal
