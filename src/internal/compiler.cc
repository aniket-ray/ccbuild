#include "internal/compiler.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <regex>

namespace ccbuild {
namespace {

namespace fs = std::filesystem;

/// Run a command and capture its first line of stdout.
std::string capture_stdout(const std::string& cmd) {
  std::array<char, 256> buf;
  std::string result;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return {};
  while (fgets(buf.data(), buf.size(), pipe))
    result += buf.data();
  pclose(pipe);
  // Trim trailing newline.
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
    result.pop_back();
  return result;
}

/// Resolve a compiler name to a full path using `which`.
/// Returns empty string if not found.
std::string resolve_path(const std::string& name) {
  auto output = capture_stdout("which " + name + " 2>/dev/null");
  if (output.empty())
    return {};
  // Verify it's executable.
  if (!fs::exists(output))
    return {};
  return output;
}

/// Identify compiler kind from version string.
CompilerKind identify_kind(const std::string& version_output) {
  if (version_output.find("clang") != std::string::npos)
    return CompilerKind::Clang;
  if (version_output.find("g++") != std::string::npos ||
      version_output.find("GCC") != std::string::npos ||
      version_output.find("gcc") != std::string::npos)
    return CompilerKind::GCC;
  return CompilerKind::Unknown;
}

/// Extract version number (e.g. "11.5.0") from --version output.
std::string extract_version(const std::string& version_output) {
  std::regex re(R"((\d+\.\d+\.\d+))");
  std::smatch match;
  if (std::regex_search(version_output, match, re))
    return match[1].str();
  return {};
}

/// Extract major version from version string.
int extract_major(const std::string& version) {
  if (version.empty())
    return 0;
  try {
    return std::stoi(version);
  } catch (...) {
    return 0;
  }
}

/// Try a single compiler candidate. Returns nullopt if not usable.
std::optional<CompilerInfo> try_compiler(const std::string& name_or_path) {
  // Resolve to full path.
  std::string path = name_or_path;
  if (!fs::exists(path))
    path = resolve_path(name_or_path);
  if (path.empty())
    return std::nullopt;

  // Get version output.
  auto version_output = capture_stdout(path + " --version 2>&1");
  if (version_output.empty())
    return std::nullopt;

  CompilerInfo info;
  info.path = path;
  info.kind = identify_kind(version_output);
  info.version = extract_version(version_output);
  info.major_version = extract_major(info.version);
  return info;
}

}  // namespace

std::string_view CompilerInfo::kind_str() const {
  switch (kind) {
  case CompilerKind::GCC:
    return "GCC";
  case CompilerKind::Clang:
    return "Clang";
  case CompilerKind::Unknown:
    return "Unknown";
  }
  return "Unknown";
}

std::optional<CompilerInfo> detect_compiler() {
  if (const char* cxx = std::getenv("CXX")) {
    if (auto info = try_compiler(cxx))
      return info;
  }

  if (auto info = try_compiler("g++"))
    return info;

  if (auto info = try_compiler("clang++"))
    return info;

  return std::nullopt;
}

}  // namespace ccbuild
