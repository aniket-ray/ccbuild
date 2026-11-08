#include "cli/driver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/version.h"
#include "support/test_support.h"

namespace ccbuild::cli {
namespace {

namespace fs = std::filesystem;
using test::TempDirTest;
using test::write_file;

using Args = std::vector<std::string_view>;

// -- parse_args ----------------------------------------------------------

TEST(ParseArgsTest, NoArguments) {
  const auto parsed = parse_args(Args{});
  EXPECT_FALSE(parsed.error.has_value());
  EXPECT_FALSE(parsed.options.show_help);
  EXPECT_FALSE(parsed.options.show_version);
}

TEST(ParseArgsTest, Help) {
  EXPECT_TRUE(parse_args(Args{ "--help" }).options.show_help);
  EXPECT_TRUE(parse_args(Args{ "-h" }).options.show_help);
}

TEST(ParseArgsTest, Version) {
  EXPECT_TRUE(parse_args(Args{ "--version" }).options.show_version);
}

TEST(ParseArgsTest, UnknownArgumentIsAnError) {
  const auto parsed = parse_args(Args{ "--frobnicate" });
  ASSERT_TRUE(parsed.error.has_value());
  EXPECT_NE(parsed.error->find("--frobnicate"), std::string::npos);
}

TEST(ParseArgsTest, UsageMentionsOptions) {
  const std::string text(usage());
  EXPECT_NE(text.find("--help"), std::string::npos);
  EXPECT_NE(text.find("--version"), std::string::npos);
}

// -- locate_installation -------------------------------------------------

using LocateInstallationTest = TempDirTest;

TEST_F(LocateInstallationTest, DefaultsToLib) {
  const auto inst = locate_installation(dir());
  EXPECT_EQ(inst.include_dir, dir() / "include");
  EXPECT_EQ(inst.lib_dir, dir() / "lib");
}

TEST_F(LocateInstallationTest, UsesLib64WhenLibrariesAreThere) {
  write_file(dir() / "lib64" / "libccbuildlib.a", "");
  EXPECT_EQ(locate_installation(dir()).lib_dir, dir() / "lib64");
}

TEST_F(LocateInstallationTest, PrefersLibWhenBothExist) {
  write_file(dir() / "lib" / "libccbuildlib.a", "");
  write_file(dir() / "lib64" / "libccbuildlib.a", "");
  EXPECT_EQ(locate_installation(dir()).lib_dir, dir() / "lib");
}

TEST(InstallationTest, LibrariesLiveInLibDir) {
  const Installation inst{ "/p/include", "/p/lib" };
  const auto libs = inst.libraries();
  ASSERT_EQ(libs.size(), 2u);
  EXPECT_EQ(libs[0], fs::path("/p/lib/libccbuildlib.a"));
  EXPECT_EQ(libs[1], fs::path("/p/lib/libninjacore.a"));
}

// -- default_installation ----------------------------------------------

TEST(DefaultInstallationTest, UsesRecordedAbsoluteDirectories) {
  const auto inst = default_installation();
  EXPECT_TRUE(inst.include_dir.is_absolute()) << inst.include_dir;
  EXPECT_TRUE(inst.lib_dir.is_absolute()) << inst.lib_dir;
}

// -- runner_fingerprint / runner_is_stale --------------------------------

class RunnerFingerprintTest : public TempDirTest {
 protected:
  void SetUp() override {
    TempDirTest::SetUp();
    write_file("build.cc", "int main() { return 0; }\n");
    write_file("lib/libccbuildlib.a", "library v1");
  }

  static std::string fingerprint(const std::vector<std::string>& command = {
                                     "c++", "build.cc" }) {
    const std::vector<fs::path> libraries = { "lib/libccbuildlib.a" };
    return runner_fingerprint(command, "build.cc", libraries);
  }

  /// Give `path` a modification time `seconds` away from its current one.
  static void shift_mtime(const fs::path& path, int seconds) {
    fs::last_write_time(
        path, fs::last_write_time(path) + std::chrono::seconds(seconds));
  }
};

TEST_F(RunnerFingerprintTest, IsDeterministic) {
  EXPECT_EQ(fingerprint(), fingerprint());
  EXPECT_NE(fingerprint().find(std::string(kVersion)), std::string::npos);
}

TEST_F(RunnerFingerprintTest, ChangesWithBuildScriptContents) {
  const auto before = fingerprint();
  const auto mtime = fs::last_write_time("build.cc");
  // Same size and same timestamp: only the content hash can tell.
  write_file("build.cc", "int main() { return 1; }\n");
  fs::last_write_time("build.cc", mtime);
  EXPECT_NE(fingerprint(), before);
}

TEST_F(RunnerFingerprintTest, ChangesWhenLibraryIsReplacedByAnOlderFile) {
  // e.g. reinstalling ccbuild from a package keeps the files' old mtimes.
  const auto before = fingerprint();
  shift_mtime("lib/libccbuildlib.a", -3600);
  EXPECT_NE(fingerprint(), before);
}

TEST_F(RunnerFingerprintTest, ChangesWithLibrarySize) {
  const auto before = fingerprint();
  const auto mtime = fs::last_write_time("lib/libccbuildlib.a");
  write_file("lib/libccbuildlib.a", "library v1 with more code");
  fs::last_write_time("lib/libccbuildlib.a", mtime);
  EXPECT_NE(fingerprint(), before);
}

TEST_F(RunnerFingerprintTest, ChangesWithCompileCommand) {
  EXPECT_NE(fingerprint({ "c++", "build.cc" }),
            fingerprint({ "clang++", "build.cc" }));
}

TEST_F(RunnerFingerprintTest, RecordsMissingInputs) {
  const auto before = fingerprint();
  fs::remove("lib/libccbuildlib.a");
  const auto after = fingerprint();
  EXPECT_NE(after, before);
  EXPECT_NE(after.find("missing"), std::string::npos);
}

TEST_F(RunnerFingerprintTest, MissingRunnerIsStale) {
  write_file("runner.stamp", fingerprint());
  EXPECT_TRUE(runner_is_stale("runner", "runner.stamp", fingerprint()));
}

TEST_F(RunnerFingerprintTest, MissingStampIsStale) {
  write_file("runner", "");
  EXPECT_TRUE(runner_is_stale("runner", "runner.stamp", fingerprint()));
}

TEST_F(RunnerFingerprintTest, MatchingStampIsFresh) {
  write_file("runner", "");
  write_file("runner.stamp", fingerprint());
  EXPECT_FALSE(runner_is_stale("runner", "runner.stamp", fingerprint()));
}

TEST_F(RunnerFingerprintTest, EditDuringCompileIsDetected) {
  // The stamp holds the fingerprint taken *before* compiling; an edit saved
  // while the compiler ran no longer matches it.
  write_file("runner", "");
  write_file("runner.stamp", fingerprint());
  write_file("build.cc", "int main() { return 2; }\n");
  EXPECT_TRUE(runner_is_stale("runner", "runner.stamp", fingerprint()));
}

// -- BuildLock -----------------------------------------------------------

using BuildLockTest = TempDirTest;

TEST_F(BuildLockTest, LocksAndCreatesTheFile) {
  const BuildLock lock("lock", /*wait=*/false);
  EXPECT_TRUE(lock.held()) << lock.error();
  EXPECT_TRUE(lock.error().empty());
  EXPECT_TRUE(fs::exists("lock"));
}

TEST_F(BuildLockTest, IsExclusive) {
  const BuildLock first("lock", /*wait=*/false);
  ASSERT_TRUE(first.held());

  const BuildLock second("lock", /*wait=*/false);
  EXPECT_FALSE(second.held());
  EXPECT_NE(second.error().find("another ccbuild"), std::string::npos);
}

TEST_F(BuildLockTest, IsReleasedOnDestruction) {
  auto first = std::make_unique<BuildLock>("lock", /*wait=*/false);
  ASSERT_TRUE(first->held());
  first.reset();

  const BuildLock second("lock", /*wait=*/false);
  EXPECT_TRUE(second.held());
}

TEST_F(BuildLockTest, ReportsUnopenableFile) {
  const BuildLock lock("missing-dir/lock", /*wait=*/false);
  EXPECT_FALSE(lock.held());
  EXPECT_NE(lock.error().find("cannot open"), std::string::npos);
}

// -- runner_compile_command ----------------------------------------------

TEST(RunnerCompileCommandTest, ContainsEverythingNeededToLink) {
  const Installation inst{ "/opt/cc build/include", "/opt/cc build/lib" };
  const auto cmd = runner_compile_command("/usr/bin/g++", inst, "build.cc",
                                          ".ccbuild/runner.tmp");

  ASSERT_GE(cmd.size(), 3u);
  EXPECT_EQ(cmd.front(), "/usr/bin/g++");
  const auto has = [&](std::string_view arg) {
    return std::ranges::find(cmd, arg) != cmd.end();
  };
  EXPECT_TRUE(has("-std=c++20"));
  // Paths with spaces stay single arguments; no shell is involved.
  EXPECT_TRUE(has("-I/opt/cc build/include"));
  EXPECT_TRUE(has("-L/opt/cc build/lib"));
  EXPECT_TRUE(has("build.cc"));
  EXPECT_TRUE(has("-lccbuildlib"));
  EXPECT_TRUE(has("-lninjacore"));
  EXPECT_EQ(cmd[cmd.size() - 2], "-o");
  EXPECT_EQ(cmd.back(), ".ccbuild/runner.tmp");
}

TEST(RunnerCompileCommandTest, CcbuildLibraryPrecedesNinjaCore) {
  const auto cmd =
      runner_compile_command("c++", Installation{ "i", "l" }, "build.cc", "r");
  const auto lib = std::ranges::find(cmd, "-lccbuildlib");
  const auto ninja = std::ranges::find(cmd, "-lninjacore");
  ASSERT_NE(lib, cmd.end());
  ASSERT_NE(ninja, cmd.end());
  EXPECT_LT(lib, ninja);  // ccbuildlib depends on ninjacore
}

// -- find_host_compiler --------------------------------------------------

TEST(FindHostCompilerTest, FindsACompiler) {
  const auto compiler = find_host_compiler();
  ASSERT_TRUE(compiler.has_value());
  EXPECT_TRUE(fs::exists(*compiler));
}

// -- run -----------------------------------------------------------------

using RunTest = TempDirTest;

TEST_F(RunTest, VersionPrintsVersion) {
  testing::internal::CaptureStdout();
  const int rc = run(Args{ "--version" });
  const std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, "ccbuild " + std::string(kVersion) + "\n");
}

TEST_F(RunTest, HelpPrintsUsage) {
  testing::internal::CaptureStdout();
  const int rc = run(Args{ "--help" });
  const std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(rc, 0);
  EXPECT_EQ(out, usage());
}

TEST_F(RunTest, UnknownArgumentIsAUsageError) {
  testing::internal::CaptureStderr();
  const int rc = run(Args{ "--bogus" });
  const std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(rc, 2);
  EXPECT_NE(err.find("unknown argument '--bogus'"), std::string::npos);
  EXPECT_FALSE(fs::exists(".ccbuild"));  // nothing was built
}

TEST_F(RunTest, MissingBuildScriptIsAnError) {
  testing::internal::CaptureStderr();
  const int rc = run(Args{});
  const std::string err = testing::internal::GetCapturedStderr();

  EXPECT_EQ(rc, 1);
  EXPECT_NE(err.find("no build.cc found"), std::string::npos);
}

// -- build_project (end to end) ------------------------------------------

/// Compiles real build scripts against this build tree's ccbuild headers and
/// libraries, exercising the full CLI flow: compile build.cc into the runner,
/// cache it, run it, and report its status.
class BuildProjectTest : public TempDirTest {
 protected:
  struct Result {
    int rc = 0;
    std::string out;  // stdout + stderr
  };

  static Result build() {
    const Installation installation{ CCBUILD_TEST_INCLUDE_DIR,
                                     CCBUILD_TEST_LIB_DIR };
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    Result result;
    result.rc = build_project(installation);
    result.out = testing::internal::GetCapturedStdout() +
                 testing::internal::GetCapturedStderr();
    return result;
  }

  static void write_build_script(std::string_view contents) {
    write_file("build.cc", contents);
  }

  static constexpr std::string_view kHelloScript = R"(
#include <ccbuild/ccbuild.h>
int main() {
  ccbuild::Project p("hello");
  p.add_executable("hello", {"main.cc"});
  return p.build();
}
)";
};

TEST_F(BuildProjectTest, CompilesAndRunsBuildScript) {
  write_file("main.cc", "int main() { return 0; }\n");
  write_build_script(kHelloScript);

  const auto first = build();
  EXPECT_EQ(first.rc, 0) << first.out;
  EXPECT_NE(first.out.find("compiling build.cc"), std::string::npos);
  EXPECT_TRUE(fs::exists(".ccbuild/runner"));
  EXPECT_TRUE(fs::exists(".ccbuild/runner.stamp"));
  EXPECT_FALSE(fs::exists(".ccbuild/runner.tmp"));
  EXPECT_TRUE(fs::exists(".ccbuild/bin/hello"));
  EXPECT_FALSE(fs::exists(".ninja_lock"));  // Ninja's files stay in .ccbuild

  // The runner is cached: a second build does not recompile build.cc.
  const auto second = build();
  EXPECT_EQ(second.rc, 0) << second.out;
  EXPECT_EQ(second.out.find("compiling build.cc"), std::string::npos);
  EXPECT_NE(second.out.find("nothing to do"), std::string::npos);
}

TEST_F(BuildProjectTest, PropagatesBuildScriptExitCode) {
  write_build_script("int main() { return 3; }\n");
  EXPECT_EQ(build().rc, 3);
}

TEST_F(BuildProjectTest, RewrittenBuildScriptIsRecompiled) {
  // Same length, written within the same second: only the content differs.
  write_build_script("int main() { return 3; }\n");
  EXPECT_EQ(build().rc, 3);
  write_build_script("int main() { return 4; }\n");
  EXPECT_EQ(build().rc, 4);
}

TEST_F(BuildProjectTest, ReportsBuildScriptCrash) {
  write_build_script("#include <cstdlib>\nint main() { std::abort(); }\n");

  const auto result = build();
  EXPECT_EQ(result.rc, 128 + SIGABRT);
  EXPECT_NE(result.out.find("build script killed by signal"),
            std::string::npos);
}

TEST_F(BuildProjectTest, BuildScriptCompileErrorKeepsPreviousRunner) {
  write_file("main.cc", "int main() { return 0; }\n");
  write_build_script(kHelloScript);
  ASSERT_EQ(build().rc, 0);

  const auto runner_time = fs::last_write_time(".ccbuild/runner");
  write_build_script("int main() { this is not C++ }\n");
  const auto result = build();
  EXPECT_NE(result.rc, 0);
  EXPECT_NE(result.out.find("failed to compile build.cc"), std::string::npos);
  EXPECT_NE(result.out.find("command:"), std::string::npos);
  // The previous runner is left intact, and no partial output remains.
  EXPECT_EQ(fs::last_write_time(".ccbuild/runner"), runner_time);
  EXPECT_FALSE(fs::exists(".ccbuild/runner.tmp"));
}

}  // namespace
}  // namespace ccbuild::cli
