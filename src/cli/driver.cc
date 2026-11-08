#include "cli/driver.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "ccbuild/version.h"
#include "internal/layout.h"
#include "internal/process.h"

// Values recorded at build time by CMake (see CMakeLists.txt):
//   CCBUILD_INSTALL_INCLUDEDIR, CCBUILD_INSTALL_LIBDIR
//       where ccbuild's headers and libraries are installed;
//   CCBUILD_HOST_CXX
//       the compiler that built them;
//   CCBUILD_RUNNER_FLAGS
//       extra flags runners need when the libraries are instrumented
//       (coverage, sanitizers).
// None are defined when ccbuild is built some other way, e.g. self-hosted.

namespace ccbuild::cli {

namespace fs = std::filesystem;
namespace layout = internal::layout;

namespace {

/// Exit code for invalid command-line usage.
constexpr int kUsageExitCode = 2;

/// Name of ccbuild's own library, used to locate the installation.
constexpr std::string_view kCoreLibrary = "libccbuildlib.a";

/// Split a space-separated list of flags.
[[maybe_unused]] std::vector<std::string> split_flags(std::string_view flags) {
  std::vector<std::string> out;
  std::size_t begin = 0;
  while (begin < flags.size()) {
    std::size_t end = flags.find(' ', begin);
    if (end == std::string_view::npos) {
      end = flags.size();
    }
    if (end > begin) {
      out.emplace_back(flags.substr(begin, end - begin));
    }
    begin = end + 1;
  }
  return out;
}

/// Read a whole file, or std::nullopt if it cannot be read.
std::optional<std::string> read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

/// Replace `path` with `contents` atomically (write a temporary file, then
/// rename it over `path`).
bool write_file_atomically(const fs::path& path, std::string_view contents) {
  fs::path temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!out.flush()) {
      return false;
    }
  }
  std::error_code ec;
  fs::rename(temp, path, ec);
  return !ec;
}

/// "<size> <mtime in ns>" for an existing file, "missing" otherwise.
std::string describe_file(const fs::path& path) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec) {
    return "missing";
  }
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return "missing";
  }
  const auto mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            mtime.time_since_epoch())
                            .count();
  return std::to_string(size) + " " + std::to_string(mtime_ns);
}

/// Compile build.cc into the runner, recording `fingerprint` beside it.
///
/// The compiler writes to a temporary file that replaces the runner only on
/// success, so a failed or interrupted compile never leaves a broken runner.
///
/// @return 0 on success, otherwise the exit code to report.
int compile_runner(const std::string& compiler,
                   const Installation& installation,
                   const std::string& fingerprint) {
  const fs::path runner(layout::kRunnerPath);
  fs::path temp = runner;
  temp += ".tmp";

  const auto command = runner_compile_command(
      compiler, installation, fs::path(layout::kBuildScript), temp);

  std::fprintf(stderr, "ccbuild: compiling %s...\n",
               std::string(layout::kBuildScript).c_str());
  const auto status = internal::run_process(command);
  if (!status.success()) {
    std::fprintf(stderr, "ccbuild: error: failed to compile %s (compiler %s)\n",
                 std::string(layout::kBuildScript).c_str(),
                 status.describe().c_str());
    std::fprintf(stderr, "  command: %s\n",
                 internal::format_command(command).c_str());
    std::error_code ec;
    fs::remove(temp, ec);
    return status.shell_exit_code();  // non-zero whenever !success()
  }

  std::error_code ec;
  fs::rename(temp, runner, ec);
  if (ec) {
    std::fprintf(stderr, "ccbuild: error: cannot install %s: %s\n",
                 runner.c_str(), ec.message().c_str());
    return 1;
  }
  if (!write_file_atomically(fs::path(layout::kRunnerStampPath), fingerprint)) {
    // The runner is fine; it will just be recompiled next time.
    std::fprintf(stderr, "ccbuild: warning: cannot write %s\n",
                 std::string(layout::kRunnerStampPath).c_str());
  }
  return 0;
}

/// Run the compiled build script.
int run_runner() {
  const std::vector<std::string> command = { std::string(layout::kRunnerPath) };
  const auto status = internal::run_process(command);
  if (status.kind != internal::ProcessStatus::Kind::kExited) {
    // A normal non-zero exit was already explained by the runner itself.
    std::fprintf(stderr, "ccbuild: build script %s\n",
                 status.describe().c_str());
  }
  return status.shell_exit_code();
}

}  // namespace

// -- Command line --------------------------------------------------------

ParsedArgs parse_args(std::span<const std::string_view> args) {
  ParsedArgs parsed;
  for (const std::string_view arg : args) {
    if (arg == "-h" || arg == "--help") {
      parsed.options.show_help = true;
    } else if (arg == "--version") {
      parsed.options.show_version = true;
    } else {
      parsed.error = "unknown argument '" + std::string(arg) + "'";
      break;
    }
  }
  return parsed;
}

std::string_view usage() {
  return "usage: ccbuild [options]\n"
         "\n"
         "Compiles ./build.cc into a cached runner (.ccbuild/runner) and runs "
         "it\n"
         "to build the project.  All build output goes to .ccbuild/.\n"
         "\n"
         "options:\n"
         "  -h, --help    show this help and exit\n"
         "  --version     print the ccbuild version and exit\n";
}

// -- Installation --------------------------------------------------------

std::vector<fs::path> Installation::libraries() const {
  return { lib_dir / kCoreLibrary, lib_dir / "libninjacore.a" };
}

Installation locate_installation(const fs::path& prefix) {
  Installation installation{ prefix / "include", prefix / "lib" };

  // Some distributions install libraries to lib64.
  std::error_code ec;
  if (!fs::exists(installation.lib_dir / kCoreLibrary, ec) &&
      fs::exists(prefix / "lib64" / kCoreLibrary, ec)) {
    installation.lib_dir = prefix / "lib64";
  }
  return installation;
}

Installation default_installation() {
#if defined(CCBUILD_INSTALL_INCLUDEDIR) && defined(CCBUILD_INSTALL_LIBDIR)
  return Installation{ CCBUILD_INSTALL_INCLUDEDIR, CCBUILD_INSTALL_LIBDIR };
#else
  // Nothing recorded at build time: assume the conventional prefix.
  return locate_installation(fs::path("/usr/local"));
#endif
}

std::optional<std::string> find_host_compiler() {
#ifdef CCBUILD_HOST_CXX
  if (auto path = internal::find_executable(CCBUILD_HOST_CXX)) {
    return path;
  }
#endif
  for (const std::string_view name : { "c++", "g++", "clang++" }) {
    if (auto path = internal::find_executable(name)) {
      return path;
    }
  }
  return std::nullopt;
}

// -- Runner --------------------------------------------------------------

std::vector<std::string> runner_compile_command(
    const std::string& compiler, const Installation& installation,
    const fs::path& build_script, const fs::path& output) {
  std::vector<std::string> command = {
    compiler,
    "-std=c++20",
    "-g",
    "-I" + installation.include_dir.string(),
    build_script.string(),
    "-L" + installation.lib_dir.string(),
    "-lccbuildlib",
    "-lninjacore",
    "-lpthread",
  };
#ifdef CCBUILD_RUNNER_FLAGS
  // ccbuild's libraries are instrumented, so the runner must link the
  // matching runtime.
  for (auto& flag : split_flags(CCBUILD_RUNNER_FLAGS)) {
    command.push_back(std::move(flag));
  }
#endif
  command.emplace_back("-o");
  command.push_back(output.string());
  return command;
}

std::string runner_fingerprint(std::span<const std::string> command,
                               const fs::path& build_script,
                               std::span<const fs::path> libraries) {
  std::string fingerprint = "ccbuild " + std::string(kVersion) + "\n";
  fingerprint += "command " + internal::format_command(command) + "\n";

  fingerprint +=
      "script " + build_script.string() + " " + describe_file(build_script);
  if (const auto contents = read_file(build_script)) {
    // Also hash the contents: timestamps can be too coarse to tell two
    // quick edits of the same size apart.
    std::array<char, 2 * sizeof(std::size_t)> hex{};
    const std::size_t hash = std::hash<std::string_view>{}(*contents);
    const auto result =
        std::to_chars(hex.data(), hex.data() + hex.size(), hash, 16);
    fingerprint += ' ';
    fingerprint.append(hex.data(), result.ptr);
  }
  fingerprint += '\n';

  for (const auto& library : libraries) {
    fingerprint +=
        "library " + library.string() + " " + describe_file(library) + "\n";
  }
  return fingerprint;
}

bool runner_is_stale(const fs::path& runner, const fs::path& stamp,
                     std::string_view fingerprint) {
  std::error_code ec;
  if (!fs::exists(runner, ec)) {
    return true;
  }
  const auto recorded = read_file(stamp);
  return !recorded || *recorded != fingerprint;
}

// -- Entry points --------------------------------------------------------

int build_project(const Installation& installation) {
  const fs::path build_script(layout::kBuildScript);
  std::error_code ec;
  if (!fs::exists(build_script, ec)) {
    std::fprintf(stderr, "ccbuild: error: no %s found in current directory.\n",
                 build_script.c_str());
    return 1;
  }

  fs::create_directories(layout::kBuildDir, ec);
  if (ec) {
    std::fprintf(stderr, "ccbuild: error: cannot create %s: %s\n",
                 std::string(layout::kBuildDir).c_str(), ec.message().c_str());
    return 1;
  }

  const auto compiler = find_host_compiler();
  if (!compiler) {
    std::fprintf(stderr,
                 "ccbuild: error: no C++ compiler found to compile %s.\n"
                 "  Install c++, g++ or clang++.\n",
                 build_script.c_str());
    return 1;
  }

  // Fingerprint the inputs before compiling, so that anything changed while
  // the compiler runs is detected next time.
  const fs::path runner(layout::kRunnerPath);
  const std::string fingerprint = runner_fingerprint(
      runner_compile_command(*compiler, installation, build_script, runner),
      build_script, installation.libraries());

  if (runner_is_stale(runner, fs::path(layout::kRunnerStampPath),
                      fingerprint)) {
    if (const int rc = compile_runner(*compiler, installation, fingerprint);
        rc != 0) {
      return rc;
    }
  }
  return run_runner();
}

int run(std::span<const std::string_view> args) {
  const ParsedArgs parsed = parse_args(args);
  if (parsed.error) {
    std::fprintf(stderr, "ccbuild: error: %s\n%.*s", parsed.error->c_str(),
                 static_cast<int>(usage().size()), usage().data());
    return kUsageExitCode;
  }
  if (parsed.options.show_help) {
    std::fwrite(usage().data(), 1, usage().size(), stdout);
    return 0;
  }
  if (parsed.options.show_version) {
    std::printf("ccbuild %.*s\n", static_cast<int>(kVersion.size()),
                kVersion.data());
    return 0;
  }
  return build_project(default_installation());
}

}  // namespace ccbuild::cli
