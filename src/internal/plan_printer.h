#ifndef CCBUILD_INTERNAL_PLAN_PRINTER_H
#define CCBUILD_INTERNAL_PLAN_PRINTER_H

#include <cstdio>

namespace ccbuild {
class Project;
}  // namespace ccbuild

namespace ccbuild::internal {

/// Print a human-readable build plan for a validated project: one block
/// per target (kind, artifact, sources, options, include directories, link
/// dependencies, and the compile/link steps), then a one-line summary.
void print_plan(const Project& project, std::FILE* out);

}  // namespace ccbuild::internal

#endif  // CCBUILD_INTERNAL_PLAN_PRINTER_H
