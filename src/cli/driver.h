#ifndef CCBUILD_CLI_DRIVER_H
#define CCBUILD_CLI_DRIVER_H

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The `ccbuild` command-line driver.
///
/// The driver compiles the project's build.cc into a cached "runner"
/// executable (linked against libccbuildlib and libninjacore) and runs it.
/// The runner's main() describes the project and calls Project::build(),
/// which validates the model and drives Ninja.
namespace ccbuild::cli {

/// Command-line options.
struct Options {
  bool show_help = false;
  bool show_version = false;
};

/// Outcome of parsing the command line.
struct ParsedArgs {
  Options options;
  std::optional<std::string> error;  ///< Set if the arguments are invalid.
};

/// Parse command-line arguments (excluding argv[0]).
[[nodiscard]] ParsedArgs parse_args(std::span<const std::string_view> args);

/// The usage text printed for --help.
[[nodiscard]] std::string_view usage();

/// Where ccbuild's headers and libraries are installed.
struct Installation {
  std::filesystem::path include_dir;  ///< Contains ccbuild/ccbuild.h.
  std::filesystem::path lib_dir;      ///< Contains libccbuildlib.a.

  /// The static libraries every runner links against.
  [[nodiscard]] std::vector<std::filesystem::path> libraries() const;
};

/// Locate the installation under `prefix`, using <prefix>/lib64 for
/// libraries when that is where they were installed.
[[nodiscard]] Installation locate_installation(
    const std::filesystem::path& prefix);

/// The installation this ccbuild was configured with: the include and
/// library directories recorded at build time, or (for builds that record
/// none, e.g. self-hosted ones) locate_installation("/usr/local").
[[nodiscard]] Installation default_installation();

/// Find the compiler used to compile build.cc into the runner.
///
/// Prefers the compiler that built ccbuild itself (recorded at build time),
/// since the runner links against libraries it produced; otherwise the
/// first of c++, g++, clang++ found on PATH.
[[nodiscard]] std::optional<std::string> find_host_compiler();

/// The command that compiles `build_script` into the executable `output`.
[[nodiscard]] std::vector<std::string> runner_compile_command(
    const std::string& compiler, const Installation& installation,
    const std::filesystem::path& build_script,
    const std::filesystem::path& output);

/// Whether the runner must be (re)compiled: it is missing, or not newer
/// than the build script.
[[nodiscard]] bool runner_is_stale(const std::filesystem::path& runner,
                                   const std::filesystem::path& build_script);

/// Build the project in the current directory: compile build.cc into the
/// runner (if the runner is stale) against `installation`, then run it.
///
/// @return The process exit code.
[[nodiscard]] int build_project(const Installation& installation);

/// Run the driver: parse `args` (excluding argv[0]), then handle --help /
/// --version or build the project in the current directory using
/// default_installation().
///
/// @return The process exit code.
[[nodiscard]] int run(std::span<const std::string_view> args);

}  // namespace ccbuild::cli

#endif  // CCBUILD_CLI_DRIVER_H
