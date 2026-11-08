#include "internal/link_graph.h"

#include <algorithm>
#include <functional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ccbuild::internal {

std::vector<const Target*> link_order(const Target& target) {
  // Reverse post-order of a depth-first search is a topological order:
  // every node comes before everything reachable from it.  Visiting each
  // node's dependencies in reverse declaration order makes the reversed
  // result list independent siblings in declaration order.
  std::vector<const Target*> post_order;
  std::unordered_set<const Target*> visited{ &target };

  std::function<void(const Target&)> visit = [&](const Target& t) {
    for (const Target& dep : t.link_deps() | std::views::reverse) {
      if (visited.insert(&dep).second) {
        visit(dep);
      }
    }
    post_order.push_back(&t);
  };
  visit(target);

  post_order.pop_back();  // `target` itself, which finishes last
  std::ranges::reverse(post_order);
  return post_order;
}

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

std::vector<std::string> link_flags(const Target& target) {
  const auto own = target.link_options();
  std::vector<std::string> flags(own.begin(), own.end());
  for (const Target* lib : link_order(target)) {
    const auto inherited = lib->link_options();
    flags.insert(flags.end(), inherited.begin(), inherited.end());
  }
  return flags;
}

}  // namespace ccbuild::internal
