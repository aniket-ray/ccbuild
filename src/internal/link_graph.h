#ifndef CCBUILD_INTERNAL_LINK_GRAPH_H
#define CCBUILD_INTERNAL_LINK_GRAPH_H

#include <string>
#include <vector>

#include "ccbuild/target.h"

/// Queries over the target link graph.
///
/// These functions define how settings propagate along `link()` edges.
/// They assume a validated project: the link graph must be acyclic.
namespace ccbuild::internal {

/// Every library `target` links against, directly or transitively, each
/// listed once, in an order suitable for a static link: every library
/// appears before all of the libraries it depends on.  Libraries without an
/// ordering constraint between them keep their declaration order.
[[nodiscard]] std::vector<const Target*> link_order(const Target& target);

/// The include directories used to compile `target`'s own sources, in
/// search order and without duplicates:
///   1. its Private, then Public directories;
///   2. the Public, then Interface directories of each transitive link
///      dependency, visited depth-first in declaration order.
[[nodiscard]] std::vector<std::string> compile_include_dirs(
    const Target& target);

}  // namespace ccbuild::internal

#endif  // CCBUILD_INTERNAL_LINK_GRAPH_H
