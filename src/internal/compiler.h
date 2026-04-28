#ifndef CCBUILD_COMPILER_H
#define CCBUILD_COMPILER_H
#include <optional>
#include <string>

namespace ccbuild {
// compiler family
enum class CompilerKind { GCC, Clang, Unknown };

// compiler info
struct CompilerInfo {
  std::string path;
  CompilerKind kind = CompilerKind::Unknown;
  std::string version;
  int major_version = 0;

  [[nodiscard]] std::string_view kind_str() const;
};

[[nodiscard]] std::optional<CompilerInfo> detect_compiler();

}  // namespace ccbuild

#endif  // CCBUILD_COMPILER_H
