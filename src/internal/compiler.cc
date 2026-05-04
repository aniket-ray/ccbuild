#include "internal/compiler.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>

namespace ccbuild {

// -- CompilerKind Output -------------------------------------------------

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

// -- Compiler Identification ---------------------------------------------

namespace internal {

CompilerKind identify_kind(std::string_view version_output) {
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
  if (version.empty()) {
    return 0;
  }
  try {
    return std::stoi(version);
  } catch (...) {
    return 0;
  }
}

}  // namespace internal

// -- Compiler Detection Helpers (file-local) -----------------------------

namespace {

namespace fs = std::filesystem;

/// Run a shell command and capture its stdout (first line, trimmed).
/// Returns an empty string if the command fails or produces no output.
std::string capture_stdout(const std::string& cmd) {
  static constexpr std::size_t kBufferSize = 256;
  std::array<char, kBufferSize> buf{};

  FILE* pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }

  std::string result;
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) !=
         nullptr) {
    result += buf.data();
  }
  pclose(pipe);

  // Trim trailing newline / carriage return.
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
}

/// Resolve a compiler name to a full path using `which`.
/// Returns an empty string if the command is not found or not executable.
std::string resolve_path(const std::string& name) {
  const auto output = capture_stdout("which " + name + " 2>/dev/null");
  if (output.empty()) {
    return {};
  }
  if (!fs::exists(output)) {
    return {};
  }
  return output;
}

/// Try to detect compiler info from a name or path candidate.
/// Returns std::nullopt if the candidate cannot be found or produces
/// no version output.
std::optional<CompilerInfo> try_compiler(const std::string& name_or_path) {
  // Resolve to full path if the candidate is not an absolute/existing path.
  std::string path = name_or_path;
  if (!fs::exists(path)) {
    path = resolve_path(name_or_path);
  }
  if (path.empty()) {
    return std::nullopt;
  }

  // Run --version and parse the output.
  const auto version_output = capture_stdout(path + " --version 2>&1");
  if (version_output.empty()) {
    return std::nullopt;
  }

  CompilerInfo info;
  info.path = path;
  info.kind = internal::identify_kind(version_output);
  info.version = internal::extract_version(version_output);
  info.major_version = internal::extract_major(info.version);
  return info;
}

}  // namespace

// -- Public API ----------------------------------------------------------

std::optional<CompilerInfo> detect_compiler() {
  // Honour the $CXX environment variable if set and usable.
  if (const char* cxx = std::getenv("CXX")) {
    if (auto info = try_compiler(cxx)) {
      return info;
    }
  }

  // Try g++ as the most common default.
  if (auto info = try_compiler("g++")) {
    return info;
  }

  // Fall back to clang++.
  if (auto info = try_compiler("clang++")) {
    return info;
  }

  return std::nullopt;
}

}  // namespace ccbuild
