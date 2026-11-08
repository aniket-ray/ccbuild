#include "cli/driver.h"

#include <cstdio>
#include <filesystem>
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

/// Compile build.cc into the runner.
///
/// The compiler writes to a temporary file that replaces the runner only on
/// success, so a failed or interrupted compile never leaves a broken runner.
///
/// @return 0 on success, otherwise the exit code to report.
int compile_runner(const std::string& compiler,
                   const Installation& installation) {
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

bool runner_is_stale(const fs::path& runner, const fs::path& build_script) {
  std::error_code ec;
  const auto runner_time = fs::last_write_time(runner, ec);
  if (ec) {
    return true;  // missing or unreadable: (re)compile
  }
  const auto script_time = fs::last_write_time(build_script, ec);
  return ec || script_time >= runner_time;
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

  if (runner_is_stale(fs::path(layout::kRunnerPath), build_script)) {
    const auto compiler = find_host_compiler();
    if (!compiler) {
      std::fprintf(stderr,
                   "ccbuild: error: no C++ compiler found to compile %s.\n"
                   "  Install c++, g++ or clang++.\n",
                   build_script.c_str());
      return 1;
    }
    if (const int rc = compile_runner(*compiler, installation); rc != 0) {
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
