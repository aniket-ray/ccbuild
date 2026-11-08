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

// -- runner_is_stale ---------------------------------------------------

class RunnerIsStaleTest : public TempDirTest {
 protected:
  /// Create `path` with an mtime `offset_seconds` from a fixed base.
  static void touch(const fs::path& path, int offset_seconds) {
    write_file(path, "");
    fs::last_write_time(path, base_ + std::chrono::seconds(offset_seconds));
  }

 private:
  static inline const fs::file_time_type base_ =
      fs::file_time_type::clock::now() - std::chrono::hours(1);
};

TEST_F(RunnerIsStaleTest, MissingRunnerIsStale) {
  touch("build.cc", 0);
  EXPECT_TRUE(runner_is_stale("runner", "build.cc"));
}

TEST_F(RunnerIsStaleTest, RunnerNewerThanScriptIsFresh) {
  touch("build.cc", 0);
  touch("runner", 1);
  EXPECT_FALSE(runner_is_stale("runner", "build.cc"));
}

TEST_F(RunnerIsStaleTest, NewerScriptMakesRunnerStale) {
  touch("runner", 0);
  touch("build.cc", 1);
  EXPECT_TRUE(runner_is_stale("runner", "build.cc"));
}

TEST_F(RunnerIsStaleTest, EqualTimestampsAreStale) {
  touch("build.cc", 0);
  touch("runner", 0);
  EXPECT_TRUE(runner_is_stale("runner", "build.cc"));
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

  /// Write build.cc.  Any cached runner is backdated so that it is older
  /// than the new script even on filesystems with coarse timestamps.
  static void write_build_script(std::string_view contents) {
    write_file("build.cc", contents);
    if (fs::exists(".ccbuild/runner")) {
      fs::last_write_time(".ccbuild/runner", fs::file_time_type::clock::now() -
                                                 std::chrono::minutes(1));
    }
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

  write_build_script("int main() { this is not C++ }\n");
  const auto runner_time = fs::last_write_time(".ccbuild/runner");
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
