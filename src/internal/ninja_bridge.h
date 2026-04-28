#ifndef CCBUILD_NINJA_BRIDGE_H
#define CCBUILD_NINJA_BRIDGE_H

namespace ccbuild {

class Project;
/// Bridge between ccbuild's project model and ninja's internal build system
/// populates ninja's state with Rules and Edges derived from the Project
class NinjaBridge {
 public:
  /// Build the project using ninja's in-memory engine.
  /// @return 0 on success, non-zero on failure
  [[nodiscard]] static int build(const Project& project);
};

}  // namespace ccbuild

#endif  // CCBUILD_NINJA_BRIDGE_H
