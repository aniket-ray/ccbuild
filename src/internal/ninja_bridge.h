#ifndef CCBUILD_INTERNAL_NINJA_BRIDGE_H
#define CCBUILD_INTERNAL_NINJA_BRIDGE_H

namespace ccbuild {
class Project;
}  // namespace ccbuild

namespace ccbuild::internal {

/// Bridge between ccbuild's project model and Ninja's in-memory build engine.
///
/// Translates the Project / Target graph into Ninja rules and edges, loads
/// the build and deps logs (for incremental builds), and runs Ninja's
/// Builder.  Stateless: the only entry point is build().
class NinjaBridge {
 public:
  /// Build the project using Ninja's in-memory engine.
  ///
  ///  1. Detects the C++ compiler.
  ///  2. Creates the Ninja rules (cc, link, ar).
  ///  3. Adds an edge per object file and per final artifact.
  ///  4. Loads the existing build/deps logs for incremental builds.
  ///  5. Runs the Ninja Builder.
  ///
  /// @param project  A fully configured, validated Project.
  /// @return 0 on success, non-zero on build failure.
  [[nodiscard]] static int build(const Project& project);
};

}  // namespace ccbuild::internal

#endif  // CCBUILD_INTERNAL_NINJA_BRIDGE_H
