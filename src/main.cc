#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Paths embedded by CMake at compile time.
#ifndef CCBUILD_INCLUDE_DIR
#error "CCBUILD_INCLUDE_DIR must be defined by CMake"
#endif
#ifndef CCBUILD_LIB_DIR
#error "CCBUILD_LIB_DIR must be defined by CMake"
#endif
#ifndef CCBUILD_NINJA_INCLUDE_DIR
#error "CCBUILD_NINJA_INCLUDE_DIR must be defined by CMake"
#endif

static const std::string build_file = "build.cc";
static const std::string runner_dir = ".ccbuild";
static const std::string runner_bin = ".ccbuild/runner";

static constexpr int64_t ns_per_sec = 1'000'000'000;
static constexpr int signal_exit_base = 128;

// Get the modification time of a file (nanosecond precision).
// Returns 0 if the file doesn't exist.
static int64_t file_mtime_ns(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0)
    return 0;
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_mtimespec.tv_sec) * ns_per_sec +
         st.st_mtimespec.tv_nsec;
#else
  return static_cast<int64_t>(st.st_mtim.tv_sec) * ns_per_sec +
         st.st_mtim.tv_nsec;
#endif
}
// Check if the cached runner is still valid (build.cc hasn't changed).
static bool runner_is_cached(const std::string& build_cc,
                             const std::string& runner) {
  auto runner_mtime = file_mtime_ns(runner);
  if (runner_mtime == 0)
    return false;  // Runner doesn't exist.

  auto build_mtime = file_mtime_ns(build_cc);
  return runner_mtime > build_mtime;
}

// Compile build.cc into .ccbuild/runner.
static int compile_build_cc(const std::string& build_cc,
                            const std::string& runner) {
  // Ensure output directory exists.
  fs::create_directories(runner_dir);

  // Build the compile command.
  // Link order matters: ccbuildlib first (depends on ninjacore), then
  // ninjacore.
  std::string cmd;
#ifdef __APPLE__
  cmd += "c++ -std=c++20 -g";
#else
  cmd += "g++ -std=c++20 -g";
#endif
  cmd += " -I" CCBUILD_INCLUDE_DIR;
  cmd += " -I" CCBUILD_NINJA_INCLUDE_DIR;
  cmd += " " + build_cc;
  cmd += " -L" CCBUILD_LIB_DIR;
  cmd += " -lccbuildlib -lninjacore";
#ifndef __APPLE__
  cmd += " -lstdc++fs";  // needed on older Linux GCC for std::filesystem
#endif
  cmd += " -lpthread";
#ifdef CCBUILD_COVERAGE_LINK_FLAGS
  cmd += " " CCBUILD_COVERAGE_LINK_FLAGS;
#endif
  cmd += " -o " + runner;
  cmd += " 2>&1";

  fprintf(stderr, "ccbuild: compiling %s...\n", build_cc.c_str());

  int rc = system(cmd.c_str());
  if (WIFEXITED(rc))
    rc = WEXITSTATUS(rc);
  if (rc != 0) {
    fprintf(stderr, "ccbuild: failed to compile %s\n", build_cc.c_str());
    fprintf(stderr, "  command: %s\n", cmd.c_str());
    return rc;
  }
  return 0;
}

// Run the compiled runner binary, forwarding the exit code.
static int run_runner(const std::string& runner) {
  int rc = system(runner.c_str());
  if (WIFEXITED(rc))
    return WEXITSTATUS(rc);
  if (WIFSIGNALED(rc)) {
    fprintf(stderr, "ccbuild: runner killed by signal %d\n", WTERMSIG(rc));
    return signal_exit_base + WTERMSIG(rc);
  }
  return 1;
}

int main(int argc, char* argv[]) {
  // Check for build.cc in the current directory.
  if (!fs::exists(build_file)) {
    fprintf(stderr, "ccbuild: error: no %s found in current directory.\n",
            build_file.c_str());
    return 1;
  }

  // Compile build.cc if needed (cache check).
  if (!runner_is_cached(build_file, runner_bin)) {
    int rc = compile_build_cc(build_file, runner_bin);
    if (rc != 0)
      return rc;
  }

  // Run the compiled build script.
  return run_runner(runner_bin);
}
