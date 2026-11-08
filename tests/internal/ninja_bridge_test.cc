#include "internal/ninja_bridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "ccbuild/project.h"
#include "internal/process.h"
#include "support/test_support.h"

namespace ccbuild::internal {
namespace {

namespace fs = std::filesystem;
using test::write_file;

/// Output of a NinjaBridge::build() run.
struct BuildOutput {
  int rc = 0;
  std::string out;
  std::string err;

  [[nodiscard]] bool mentions_error() const {
    return out.find("error") != std::string::npos ||
           err.find("error") != std::string::npos;
  }
};

/// Every test runs in its own empty temporary directory.
class NinjaBridgeTest : public test::TempDirTest {
 protected:
  static BuildOutput build(const Project& p) {
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    BuildOutput result;
    result.rc = NinjaBridge::build(p);
    result.out = testing::internal::GetCapturedStdout();
    result.err = testing::internal::GetCapturedStderr();
    return result;
  }

  /// Run a built executable and return its exit code.
  static int run(const std::string& path) {
    return run_process(std::vector<std::string>{ path }).shell_exit_code();
  }
};

// -- Basic builds --------------------------------------------------------

TEST_F(NinjaBridgeTest, EmptyProjectSucceeds) {
  const Project p("empty_project");

  const auto result = build(p);
  EXPECT_EQ(result.rc, 0);
  EXPECT_NE(result.out.find("nothing to do"), std::string::npos);
}

TEST_F(NinjaBridgeTest, BuildFailsOnMissingSource) {
  Project p("missing_src_project");
  p.add_executable("app", { "does_not_exist.cc" });

  const auto result = build(p);
  EXPECT_NE(result.rc, 0);
  EXPECT_TRUE(result.mentions_error());
}

TEST_F(NinjaBridgeTest, BuildSucceedsWithValidSource) {
  write_file("main.cc", "int main() { return 0; }\n");

  Project p("dummy_project");
  p.add_executable("dummy_app", { "main.cc" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists(".ccbuild/bin/dummy_app"));
  EXPECT_EQ(run(".ccbuild/bin/dummy_app"), 0);
}

TEST_F(NinjaBridgeTest, BuildSucceedsWithComplexGraph) {
  write_file("lib.cc", "int the_answer() { return 42; }\n");
  write_file("main.cc",
             "int the_answer();\n"
             "int main() { return the_answer() == 42 ? 0 : 1; }\n");

  Project p("complex_project");
  auto& lib = p.add_library("my_lib", { "lib.cc" });
  lib.add_include_dirs({ "include" }, Visibility::Public);
  lib.add_include_dirs({ "src" }, Visibility::Private);
  lib.add_compile_options({ "-DTEST_DEFINE" });

  auto& app = p.add_executable("my_app", { "main.cc" });
  app.link(lib);
  app.add_link_options({ "-O0" });
  app.add_include_dirs({ "app_include" }, Visibility::Interface);

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists(".ccbuild/lib/libmy_lib.a"));
  EXPECT_EQ(run(".ccbuild/bin/my_app"), 0);
}

// A library's link options are for the executables that link it; `ar`
// never sees them (it would reject "-pthread" as conflicting operations).
TEST_F(NinjaBridgeTest, LibraryLinkOptionsAreNotPassedToAr) {
  write_file("lib.cc", "int func() { return 1; }\n");

  Project p("ar_test");
  auto& lib = p.add_library("my_lib", { "lib.cc" });
  lib.add_link_options({ "-pthread" });

  const auto result = build(p);
  EXPECT_EQ(result.rc, 0) << result.out;
  EXPECT_TRUE(fs::exists(".ccbuild/lib/libmy_lib.a"));
}

TEST_F(NinjaBridgeTest, SecondBuildAlreadyUpToDate) {
  write_file("main.cc", "int main() { return 0; }\n");

  Project p("uptodate_test");
  p.add_executable("app", { "main.cc" });

  EXPECT_EQ(build(p).rc, 0);
  const auto second = build(p);
  EXPECT_EQ(second.rc, 0);
  EXPECT_NE(second.out.find("nothing to do"), std::string::npos);
}

TEST_F(NinjaBridgeTest, HeaderChangeTriggersRebuild) {
  write_file("value.h", "#define VALUE 1\n");
  write_file("main.cc", "#include \"value.h\"\nint main() { return VALUE; }\n");

  Project p("header_dep_test");
  p.add_executable("app", { "main.cc" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_EQ(run(".ccbuild/bin/app"), 1);

  // Bump the header's mtime so the change is visible even on filesystems
  // with coarse timestamps.
  write_file("value.h", "#define VALUE 7\n");
  fs::last_write_time("value.h",
                      fs::last_write_time("value.h") + std::chrono::seconds(2));

  const auto second = build(p);
  EXPECT_EQ(second.rc, 0);
  EXPECT_EQ(second.out.find("nothing to do"), std::string::npos);
  EXPECT_EQ(run(".ccbuild/bin/app"), 7);
}

TEST_F(NinjaBridgeTest, BuildFailsOnCompilationError) {
  write_file("main.cc", "int main() { return 1 }\n");

  Project p("comperr_test");
  p.add_executable("app", { "main.cc" });

  const auto result = build(p);
  EXPECT_NE(result.rc, 0);
  EXPECT_TRUE(result.mentions_error());
}

TEST_F(NinjaBridgeTest, SourceInSubdirectoryBuildsCorrectObject) {
  write_file("src/main.cc", "int main() { return 0; }\n");

  Project p("subdir_test");
  p.add_executable("app", { "src/main.cc" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists(".ccbuild/obj/app/src/main.o"));
}

TEST_F(NinjaBridgeTest, SourceOutsideProjectStaysInsideBuildDir) {
  fs::create_directories("project");
  write_file("shared/main.cc", "int main() { return 0; }\n");
  const test::ScopedCurrentDir in_project("project");

  Project p("outside_test");
  p.add_executable("app", { "../shared/main.cc" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists(".ccbuild/obj/app/__/shared/main.o"));
  EXPECT_FALSE(fs::exists("../shared/main.o"));
}

TEST_F(NinjaBridgeTest, TwoExecutablesBuildBoth) {
  write_file("one.cc", "int main() { return 0; }\n");
  write_file("two.cc", "int main() { return 1; }\n");

  Project p("two_exe_test");
  p.add_executable("app1", { "one.cc" });
  p.add_executable("app2", { "two.cc" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_EQ(run(".ccbuild/bin/app1"), 0);
  EXPECT_EQ(run(".ccbuild/bin/app2"), 1);
}

TEST_F(NinjaBridgeTest, MultipleLinkOptionsSeparation) {
  write_file("main.cc", "int main() { return 0; }\n");

  Project p("mlo_test");
  auto& app = p.add_executable("app", { "main.cc" });
  app.add_link_options({ "-s", "-v" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists(".ccbuild/bin/app"));
}

TEST_F(NinjaBridgeTest, CompileOptionsWithoutIncludeDirs) {
  write_file("main.cc", "int main() { return 0; }\n");

  Project p("co_test");
  auto& app = p.add_executable("app", { "main.cc" });
  app.add_compile_options({ "-Wall", "-Wextra" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists(".ccbuild/bin/app"));
}

// -- Include directory propagation ---------------------------------------

TEST_F(NinjaBridgeTest, InterfaceIncludeDirsPropagateToLinker) {
  write_file("iface_inc/util.h",
             "#pragma once\ninline int util() { return 99; }\n");
  write_file("lib.cc", "int lib_func() { return 1; }\n");
  write_file("main.cc",
             "#include \"util.h\"\n"
             "int lib_func();\n"
             "int main() { return lib_func() + util() == 100 ? 0 : 1; }\n");

  Project p("iface_test");
  auto& lib = p.add_library("mylib", { "lib.cc" });
  lib.add_include_dirs({ "iface_inc" }, Visibility::Interface);
  auto& app = p.add_executable("myapp", { "main.cc" });
  app.link(lib);

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_EQ(run(".ccbuild/bin/myapp"), 0);
}

TEST_F(NinjaBridgeTest, MultiDepthTransitiveIncludes) {
  write_file("inc_a/a.h", "#pragma once\nint func_a();\n");
  write_file("inc_b/b.h", "#pragma once\nint func_b();\n");
  write_file("a.cc", "int func_a() { return 1; }\n");
  write_file("b.cc",
             "#include \"a.h\"\n"
             "#include \"b.h\"\n"
             "int func_b() { return func_a() + 1; }\n");
  write_file("main.cc",
             "#include \"b.h\"\n"
             "int main() { return func_b() == 2 ? 0 : 1; }\n");

  Project p("trans_test");
  auto& liba = p.add_library("liba", { "a.cc" });
  liba.add_include_dirs({ "inc_a" }, Visibility::Public);

  auto& libb = p.add_library("libb", { "b.cc" });
  libb.add_include_dirs({ "inc_b" }, Visibility::Public);
  libb.link(liba);

  auto& app = p.add_executable("app", { "main.cc" });
  app.link(libb);

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists(".ccbuild/lib/libliba.a"));
  EXPECT_TRUE(fs::exists(".ccbuild/lib/liblibb.a"));
  EXPECT_EQ(run(".ccbuild/bin/app"), 0);
}

// -- Static link order ---------------------------------------------------

TEST_F(NinjaBridgeTest, DuplicateTransitiveLinkDeps) {
  write_file("base.cc", "int base_func() { return 1; }\n");
  write_file("mid1.cc",
             "int base_func();\n"
             "int mid1_func() { return base_func() + 1; }\n");
  write_file("mid2.cc",
             "int base_func();\n"
             "int mid2_func() { return base_func() + 2; }\n");
  write_file(
      "main.cc",
      "int mid1_func();\n"
      "int mid2_func();\n"
      "int main() { return (mid1_func() + mid2_func()) == 5 ? 0 : 1; }\n");

  Project p("dup_deps_test");
  auto& base = p.add_library("base", { "base.cc" });
  auto& mid1 = p.add_library("mid1", { "mid1.cc" });
  auto& mid2 = p.add_library("mid2", { "mid2.cc" });
  mid1.link(base);
  mid2.link(base);
  auto& app = p.add_executable("app", { "main.cc" });
  app.link(mid1).link(mid2);

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_EQ(run(".ccbuild/bin/app"), 0);
}

// Regression test: a shared library dependency must be linked after every
// library that uses it.  base has two object files, so a link order of
// "mid1 base mid2" pulls in only base_a.o and leaves base_b() unresolved.
TEST_F(NinjaBridgeTest, SharedDependencyIsLinkedAfterAllUsers) {
  write_file("base_a.cc", "int base_a() { return 1; }\n");
  write_file("base_b.cc", "int base_b() { return 2; }\n");
  write_file("mid1.cc", "int base_a();\nint mid1() { return base_a(); }\n");
  write_file("mid2.cc", "int base_b();\nint mid2() { return base_b(); }\n");
  write_file("main.cc",
             "int mid1();\nint mid2();\n"
             "int main() { return mid1() + mid2() == 3 ? 0 : 1; }\n");

  Project p("link_order_test");
  auto& base = p.add_library("base", { "base_a.cc", "base_b.cc" });
  auto& mid1 = p.add_library("mid1", { "mid1.cc" });
  auto& mid2 = p.add_library("mid2", { "mid2.cc" });
  mid1.link(base);
  mid2.link(base);
  p.add_executable("app", { "main.cc" }).link(mid1).link(mid2);

  const auto result = build(p);
  EXPECT_EQ(result.rc, 0) << result.out;
  EXPECT_EQ(run(".ccbuild/bin/app"), 0);
}

// -- Link options --------------------------------------------------------

// Link options come after the objects and archives, so an archive (or -l
// library) passed as a link option can resolve symbols the objects need.
TEST_F(NinjaBridgeTest, LinkOptionsFollowLinkInputs) {
  write_file("dep.cc", "int dep() { return 0; }\n");
  write_file("main.cc", "int dep();\nint main() { return dep(); }\n");

  Project lib_project("prebuilt");
  lib_project.add_library("dep", { "dep.cc" });
  ASSERT_EQ(build(lib_project).rc, 0);

  Project p("link_opts_order_test");
  p.add_executable("app", { "main.cc" })
      .add_link_options({ ".ccbuild/lib/libdep.a" });

  const auto result = build(p);
  EXPECT_EQ(result.rc, 0) << result.out;
  EXPECT_EQ(run(".ccbuild/bin/app"), 0);
}

// A static library's link options describe what its users must link, so
// they are applied when linking executables that use it.
TEST_F(NinjaBridgeTest, LibraryLinkOptionsApplyToExecutables) {
  write_file("lib.cc", "int lib() { return 0; }\n");
  write_file("main.cc", "int lib();\nint main() { return lib(); }\n");

  Project p("lib_link_opts_test");
  auto& lib = p.add_library("lib", { "lib.cc" });
  lib.add_link_options({ "-Wl,-Map,app.map" });  // observable side effect
  p.add_executable("app", { "main.cc" }).link(lib);

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_TRUE(fs::exists("app.map"));
}

// -- Argument quoting ----------------------------------------------------

TEST_F(NinjaBridgeTest, IncludeDirWithSpaces) {
  write_file("my headers/value.h", "#define VALUE 0\n");
  write_file("main.cc", "#include \"value.h\"\nint main() { return VALUE; }\n");

  Project p("space_test");
  p.add_executable("app", { "main.cc" })
      .add_include_dirs({ "my headers" }, Visibility::Private);

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_EQ(run(".ccbuild/bin/app"), 0);
}

// Options are shell syntax: one string may hold several flags, command
// substitution works, and shell quoting is honoured.
TEST_F(NinjaBridgeTest, CompileOptionsAreShellSyntax) {
  write_file("main.cc",
             "static_assert(ONE + TWO + THREE == 6, \"flags\");\n"
             "static_assert(sizeof(MSG) == 4, \"MSG is the string a b\");\n"
             "int main() { return 0; }\n");

  Project p("shell_syntax_test");
  p.add_executable("app", { "main.cc" })
      .add_compile_options(
          { "-DONE=1 -DTWO=2", "$(echo -DTHREE=3)", "-DMSG='\"a b\"'" });

  const auto result = build(p);
  EXPECT_EQ(result.rc, 0) << result.out;
}

TEST_F(NinjaBridgeTest, LinkOptionsAreShellSyntax) {
  write_file("main.cc", "int main() { return 0; }\n");

  Project p("link_shell_syntax_test");
  p.add_executable("app", { "main.cc" })
      .add_link_options({ "-Wl,-Map,one.map $(echo -pthread)" });

  const auto result = build(p);
  EXPECT_EQ(result.rc, 0) << result.out;
  EXPECT_TRUE(fs::exists("one.map"));
}

TEST_F(NinjaBridgeTest, SourcePathWithSpaces) {
  write_file("my src/main.cc", "int main() { return 0; }\n");

  Project p("src_space_test");
  p.add_executable("app", { "my src/main.cc" });

  EXPECT_EQ(build(p).rc, 0);
  EXPECT_EQ(run(".ccbuild/bin/app"), 0);
}

}  // namespace
}  // namespace ccbuild::internal
