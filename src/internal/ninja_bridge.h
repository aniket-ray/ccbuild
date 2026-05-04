#ifndef CCBUILD_NINJA_BRIDGE_H
#define CCBUILD_NINJA_BRIDGE_H

namespace ccbuild {

class Project;

/// Bridge between ccbuild's project model and Ninja's in-memory build engine.
///
/// Translates the high-level Project / Target graph into Ninja Rules and
/// Edges, sets up build/deps logs, and invokes the Ninja Builder.
///
/// This is a stateless, function-level interface -- the only public entry
/// point is the static NinjaBridge::build().
class NinjaBridge {
 public:
  /// Build the project using Ninja's in-memory engine.
  ///
  ///  1. Detects the C++ compiler.
  ///  2. Creates Ninja build rules (cc, link, ar).
  ///  3. Populates the Ninja build graph from the Project targets.
  ///  4. Loads/existing build/deps logs for incremental builds.
  ///  5. Runs the Ninja Builder.
  ///
  /// @param project  A fully configured and valid Project.
  /// @return 0 on success, non-zero on build failure.
  [[nodiscard]] static int build(const Project& project);
};

}  // namespace ccbuild

#endif  // CCBUILD_NINJA_BRIDGE_H
