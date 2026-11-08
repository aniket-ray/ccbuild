#include "internal/compiler.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

#include "internal/process.h"

namespace ccbuild::internal {

// -- CompilerInfo --------------------------------------------------------

std::string_view CompilerInfo::kind_str() const {
  switch (kind) {
  case CompilerKind::Gcc:
    return "GCC";
  case CompilerKind::Clang:
    return "Clang";
  case CompilerKind::Unknown:
    return "Unknown";
  }
  return "Unknown";
}

// -- Parsing helpers -----------------------------------------------------

CompilerKind identify_kind(std::string_view version_output) {
  // Check for clang first: clang's banner may also mention gcc
  // compatibility.
  if (version_output.find("clang") != std::string_view::npos) {
    return CompilerKind::Clang;
  }
  if (version_output.find("g++") != std::string_view::npos ||
      version_output.find("GCC") != std::string_view::npos ||
      version_output.find("gcc") != std::string_view::npos) {
    return CompilerKind::Gcc;
  }
  return CompilerKind::Unknown;
}

std::string extract_version(const std::string& version_output) {
  static const std::regex kVersionRe(R"((\d+\.\d+\.\d+))");
  std::smatch match;
  if (std::regex_search(version_output, match, kVersionRe)) {
    return match[1].str();
  }
  return {};
}

int extract_major(const std::string& version) {
  int major = 0;
  const auto* const begin = version.data();
  const auto* const end = begin + version.size();
  const auto [ptr, ec] = std::from_chars(begin, end, major);
  if (ec != std::errc() || ptr == begin) {
    return 0;
  }
  return major;
}

// -- Detection -----------------------------------------------------------

std::optional<CompilerInfo> probe_compiler(std::string_view name_or_path) {
  auto path = find_executable(name_or_path);
  if (!path) {
    return std::nullopt;
  }

  const std::array<std::string, 2> argv = { *path, "--version" };
  const auto probe = capture_process(argv);
  if (!probe.status.success() || probe.output.empty()) {
    return std::nullopt;
  }

  CompilerInfo info;
  info.path = std::move(*path);
  info.kind = identify_kind(probe.output);
  info.version = extract_version(probe.output);
  info.major_version = extract_major(info.version);
  return info;
}

std::optional<CompilerInfo> detect_compiler() {
  // Honour $CXX if set and usable.
  if (const char* cxx = std::getenv("CXX"); cxx != nullptr && *cxx != '\0') {
    if (auto info = probe_compiler(cxx)) {
      return info;
    }
    std::fprintf(stderr,
                 "ccbuild: warning: $CXX ('%s') is not a usable compiler; "
                 "falling back to g++/clang++\n",
                 cxx);
  }

  for (const std::string_view candidate : { "g++", "clang++" }) {
    if (auto info = probe_compiler(candidate)) {
      return info;
    }
  }
  return std::nullopt;
}

}  // namespace ccbuild::internal
