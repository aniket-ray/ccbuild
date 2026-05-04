#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

// -- Constants -----------------------------------------------------------

static constexpr std::string_view kBuildFile = "build.cc";
static constexpr std::string_view kRunnerDir = ".ccbuild";
static constexpr std::string_view kRunnerBin = ".ccbuild/runner";
static constexpr int64_t kNanosecondsPerSecond = 1'000'000'000;
static constexpr int kSignalExitBase = 128;

// -- Locate ccbuild's installed resources --------------------------------
//
// The install prefix is baked in at compile-time by CMake:
//   -DCCBUILD_INSTALL_PREFIX="/usr/local"
//
// Headers live at ${prefix}/include, libraries at ${prefix}/lib.
// If the binary is running from the build tree (pre-install), we fall
// back to paths relative to the binary's own location.

#ifndef CCBUILD_INSTALL_PREFIX
#define CCBUILD_INSTALL_PREFIX "/usr/local"
#endif

/// Return the absolute path of the currently running executable.
/// The ccbuild resource root directory (baked at compile time).
static const std::string kCcbuildRoot = CCBUILD_INSTALL_PREFIX;

/// Get the modification time of a file in nanoseconds since epoch.
/// @return The mtime, or 0 if the file cannot be stat'd.
static int64_t file_mtime_ns(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return 0;
  }
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_mtimespec.tv_sec) * kNanosecondsPerSecond +
         st.st_mtimespec.tv_nsec;
#else
  return static_cast<int64_t>(st.st_mtim.tv_sec) * kNanosecondsPerSecond +
         st.st_mtim.tv_nsec;
#endif
}

/// Check whether the cached runner binary is newer than build.cc.
/// If so, we can skip recompilation.
static bool runner_is_cached(const std::string& build_cc,
                             const std::string& runner) {
  const auto runner_mtime = file_mtime_ns(runner);
  if (runner_mtime == 0) {
    return false;
  }
  const auto build_mtime = file_mtime_ns(build_cc);
  return runner_mtime > build_mtime;
}

/// Compile the build.cc script into the runner binary.
/// @return 0 on success, non-zero on compilation failure.
static int compile_build_cc(const std::string& build_cc,
                            const std::string& runner) {
  fs::create_directories(kRunnerDir);

  const auto root = kCcbuildRoot;

  // Determine the library directory -- on some distros it's lib64.
  std::string lib_dir = root + "/lib";
  {
    std::error_code ec;
    if (!fs::exists(lib_dir + "/libccbuildlib.a", ec)) {
      const std::string alt = root + "/lib64";
      if (fs::exists(alt + "/libccbuildlib.a", ec)) {
        lib_dir = alt;
      }
    }
  }

  // Build the compiler command.
  std::string cmd;
#ifdef __APPLE__
  cmd += "c++ -std=c++20 -g";
#else
  cmd += "g++ -std=c++20 -g";
#endif
  cmd += " -I" + root + "/include";
  cmd += " " + build_cc;
  cmd += " -L" + lib_dir;
  cmd += " -lccbuildlib -lninjacore";
#ifndef __APPLE__
  cmd += " -lstdc++fs";
#endif
  cmd += " -lpthread";
#ifdef CCBUILD_COVERAGE_LINK_FLAGS
  cmd += " " CCBUILD_COVERAGE_LINK_FLAGS;
#endif
  cmd += " -o " + runner;
  cmd += " 2>&1";

  fprintf(stderr, "ccbuild: compiling %s...\n", build_cc.c_str());

  int rc = system(cmd.c_str());
  if (WIFEXITED(rc)) {
    rc = WEXITSTATUS(rc);
  }
  if (rc != 0) {
    fprintf(stderr, "ccbuild: failed to compile %s\n", build_cc.c_str());
    fprintf(stderr, "  command: %s\n", cmd.c_str());
    return rc;
  }
  return 0;
}

/// Execute the runner binary.
/// @return The runner's exit code, or a signal-based code if killed.
static int run_runner(const std::string& runner) {
  const int rc = system(runner.c_str());
  if (WIFEXITED(rc)) {
    return WEXITSTATUS(rc);
  }
  if (WIFSIGNALED(rc)) {
    fprintf(stderr, "ccbuild: runner killed by signal %d\n", WTERMSIG(rc));
    return kSignalExitBase + WTERMSIG(rc);
  }
  return 1;
}

// -- Entry Point ---------------------------------------------------------

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  if (!fs::exists(kBuildFile)) {
    fprintf(stderr, "ccbuild: error: no %s found in current directory.\n",
            kBuildFile.data());
    return 1;
  }

  // Recompile the runner if build.cc is newer than the cached binary.
  if (!runner_is_cached(std::string(kBuildFile), std::string(kRunnerBin))) {
    const int rc =
        compile_build_cc(std::string(kBuildFile), std::string(kRunnerBin));
    if (rc != 0) {
      return rc;
    }
  }

  return run_runner(std::string(kRunnerBin));
}
