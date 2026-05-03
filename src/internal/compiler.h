#ifndef CCBUILD_COMPILER_H
#define CCBUILD_COMPILER_H
#include <optional>
#include <string>

namespace ccbuild {

enum class CompilerKind { Gcc, Clang, Unknown };

struct CompilerInfo {
  std::string path;
  CompilerKind kind = CompilerKind::Unknown;
  std::string version;
  int major_version = 0;

  [[nodiscard]] std::string_view kind_str() const;
};

[[nodiscard]] std::optional<CompilerInfo> detect_compiler();

namespace internal {

CompilerKind identify_kind(std::string_view version_output);
std::string extract_version(const std::string& version_output);
int extract_major(const std::string& version);

}  // namespace internal
}  // namespace ccbuild

#endif  // CCBUILD_COMPILER_H
