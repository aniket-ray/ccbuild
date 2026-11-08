#include "ccbuild/project.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/executable.h"
#include "support/test_support.h"

namespace ccbuild {
namespace {

using test::TempDirTest;
using test::write_file;

/// Run a dry-run build, returning its exit code and captured stderr.
struct BuildResult {
  int rc;
  std::string err;
};
BuildResult dry_run(Project& p) {
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int rc = p.build(/*dry_run=*/true);
  testing::internal::GetCapturedStdout();
  return { rc, testing::internal::GetCapturedStderr() };
}

// -- Default Settings ----------------------------------------------------

TEST(ProjectTest, DefaultStandardIs17) {
  Project p("myproject");
  EXPECT_EQ(p.standard(), 17);
}

// -- Adding Targets ------------------------------------------------------

TEST(ProjectTest, AddExecutableReturnsReference) {
  Project p("test");
  auto& exe = p.add_executable("myapp", { "main.cc" });
  EXPECT_EQ(exe.name(), "myapp");
  EXPECT_EQ(exe.kind(), TargetKind::Executable);
}

TEST(ProjectTest, AddExecutableAppearsInTargets) {
  Project p("test");
  p.add_executable("myapp", { "main.cc" });
  ASSERT_EQ(p.targets().size(), 1u);
  EXPECT_EQ(p.targets()[0]->name(), "myapp");
}

TEST(ProjectTest, AddLibraryReturnsReference) {
  Project p("test");
  auto& lib = p.add_library("mylib", { "lib.cc" });
  EXPECT_EQ(lib.name(), "mylib");
  EXPECT_EQ(lib.kind(), TargetKind::StaticLibrary);
}

TEST(ProjectTest, AddExecutableFromVector) {
  Project p("test");
  const std::vector<std::string> sources = { "main.cc", "util.cc" };
  auto& exe = p.add_executable("myapp", sources);
  ASSERT_EQ(exe.sources().size(), 2u);
  EXPECT_EQ(exe.sources()[1], "util.cc");
}

TEST(ProjectTest, AddLibraryFromStringViews) {
  Project p("test");
  auto& lib =
      p.add_library("mylib", std::vector<std::string_view>{ "a.cc", "b.cc" });
  EXPECT_EQ(lib.sources().size(), 2u);
}

TEST(ProjectTest, TargetReferencesStayValidAsTargetsAreAdded) {
  Project p("test");
  auto& first = p.add_library("first", { "a.cc" });
  for (int i = 0; i < 100; ++i) {
    p.add_library("lib" + std::to_string(i), { "x.cc" });
  }
  EXPECT_EQ(first.name(), "first");
}

// -- Basic Build ---------------------------------------------------------

TEST(ProjectTest, BuildSucceedsOnValidProject) {
  Project p("test");
  p.add_executable("myapp", { "main.cc" });
  EXPECT_EQ(p.build(/*dry_run=*/true), 0);
}

TEST(ProjectTest, BuildSucceedsWithLinkDeps) {
  Project p("test");
  auto& lib = p.add_library("mylib", { "lib.cc" });
  auto& exe = p.add_executable("myapp", { "main.cc" });
  exe.link(lib);
  EXPECT_EQ(p.build(/*dry_run=*/true), 0);
}

// -- Validation: Duplicate Names -----------------------------------------

TEST(ProjectTest, BuildFailsOnDuplicateTargetNames) {
  Project p("test");
  p.add_executable("foo", { "a.cc" });
  p.add_executable("foo", { "b.cc" });

  testing::internal::CaptureStderr();
  int rc = p.build(/*dry_run=*/true);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("duplicate target name"), std::string::npos);
  EXPECT_NE(err.find("foo"), std::string::npos);
}

TEST(ProjectTest, BuildFailsOnDuplicateExeAndLibName) {
  Project p("test");
  p.add_executable("foo", { "a.cc" });
  p.add_library("foo", { "b.cc" });

  testing::internal::CaptureStderr();
  int rc = p.build(/*dry_run=*/true);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("duplicate target name"), std::string::npos);
}

// -- Validation: Empty Sources -------------------------------------------

TEST(ProjectTest, BuildFailsOnEmptySources) {
  Project p("test");
  p.add_executable("myapp", {});

  testing::internal::CaptureStderr();
  int rc = p.build(/*dry_run=*/true);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("no sources"), std::string::npos);
  EXPECT_NE(err.find("myapp"), std::string::npos);
}

// -- Validation: Invalid Extensions --------------------------------------

TEST(ProjectTest, BuildFailsOnInvalidSourceExtension) {
  Project p("test");
  p.add_executable("myapp", { "Makefile" });

  testing::internal::CaptureStderr();
  int rc = p.build(/*dry_run=*/true);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("invalid source file"), std::string::npos);
  EXPECT_NE(err.find("Makefile"), std::string::npos);
}

// -- Validation: Names, Standard, Object Paths ---------------------------

TEST(ProjectTest, BuildFailsOnInvalidTargetName) {
  Project p("test");
  p.add_executable("../escape", { "main.cc" });

  const auto [rc, err] = dry_run(p);
  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("invalid target name"), std::string::npos);
}

TEST(ProjectTest, BuildFailsOnUnsupportedStandard) {
  Project p("test");
  p.set_cxx_standard(21);
  p.add_executable("app", { "main.cc" });

  const auto [rc, err] = dry_run(p);
  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("unsupported C++ standard: 21"), std::string::npos);
}

TEST(ProjectTest, BuildAcceptsEverySupportedStandard) {
  for (const int standard : { 98, 11, 14, 17, 20, 23, 26 }) {
    Project p("test");
    p.set_cxx_standard(standard);
    p.add_executable("app", { "main.cc" });
    EXPECT_EQ(dry_run(p).rc, 0) << "C++" << standard;
  }
}

TEST(ProjectTest, BuildFailsOnDuplicateSource) {
  Project p("test");
  p.add_executable("app", { "main.cc", "main.cc" });

  const auto [rc, err] = dry_run(p);
  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("more than once"), std::string::npos);
}

TEST(ProjectTest, BuildFailsOnObjectPathCollision) {
  Project p("test");
  p.add_executable("app", { "a.cc", "a.cpp" });

  const auto [rc, err] = dry_run(p);
  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("both compile to"), std::string::npos);
  EXPECT_NE(err.find(".ccbuild/obj/app/a.o"), std::string::npos);
}

TEST(ProjectTest, BuildFailsOnLinkToTargetOfAnotherProject) {
  Project other("other");
  auto& foreign = other.add_library("foreign", { "f.cc" });

  Project p("test");
  p.add_executable("app", { "main.cc" }).link(foreign);

  const auto [rc, err] = dry_run(p);
  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("not part of this project"), std::string::npos);
}

// -- Validation: Executable Link Rules -----------------------------------

TEST(ProjectTest, LinkingExeToExeThrows) {
  Executable a("a", { "a.cc" });
  Executable b("b", { "b.cc" });

  EXPECT_THROW(a.link(b), std::invalid_argument);
}

TEST(ProjectTest, BuildSucceedsExeLinkingLib) {
  Project p("test");
  auto& lib = p.add_library("mylib", { "lib.cc" });
  auto& exe = p.add_executable("myexe", { "main.cc" });
  exe.link(lib);
  EXPECT_EQ(p.build(/*dry_run=*/true), 0);
}

// -- Validation: Link Cycles ---------------------------------------------

TEST(ProjectTest, BuildFailsOnDirectLinkCycle) {
  Project p("test");
  auto& a = p.add_library("a", { "a.cc" });
  auto& b = p.add_library("b", { "b.cc" });
  a.link(b);
  b.link(a);

  testing::internal::CaptureStderr();
  int rc = p.build(/*dry_run=*/true);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("link cycle"), std::string::npos);
}

TEST(ProjectTest, BuildFailsOnSelfLink) {
  Project p("test");
  auto& a = p.add_library("a", { "a.cc" });
  a.link(a);

  testing::internal::CaptureStderr();
  int rc = p.build(/*dry_run=*/true);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("link cycle"), std::string::npos);
}

TEST(ProjectTest, BuildFailsOnTransitiveLinkCycle) {
  Project p("test");

  auto& a = p.add_library("a", { "a.cc" });
  auto& b = p.add_library("b", { "b.cc" });
  auto& c = p.add_library("c", { "c.cc" });

  a.link(b);
  b.link(c);
  c.link(a);

  testing::internal::CaptureStderr();
  int rc = p.build(/*dry_run=*/true);
  std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(rc, 0);
  EXPECT_NE(err.find("link cycle: a -> b -> c -> a"), std::string::npos);
}

TEST(ProjectTest, BuildSucceedsOnDiamondDag) {
  Project p("test");
  auto& base = p.add_library("base", { "base.cc" });
  auto& mid1 = p.add_library("mid1", { "mid1.cc" });
  auto& mid2 = p.add_library("mid2", { "mid2.cc" });
  auto& app = p.add_executable("app", { "app.cc" });

  mid1.link(base);
  mid2.link(base);
  app.link(mid1).link(mid2);

  testing::internal::CaptureStdout();
  EXPECT_EQ(p.build(/*dry_run=*/true), 0);
  testing::internal::GetCapturedStdout();
}

// -- Build Output --------------------------------------------------------

TEST(ProjectTest, BuildOutputContainsProjectName) {
  Project p("coolproject");
  p.set_cxx_standard(20);
  p.add_executable("app", { "main.cc" });

  testing::internal::CaptureStdout();
  static_cast<void>(p.build(/*dry_run=*/true));
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_NE(out.find("coolproject"), std::string::npos);
  EXPECT_NE(out.find("C++20"), std::string::npos);
}

TEST(ProjectTest, BuildOutputContainsTargetInfo) {
  Project p("test");
  auto& lib = p.add_library("mylib", { "src/lib.cc" });
  auto& exe = p.add_executable("myapp", { "src/main.cc" });
  exe.link(lib);
  exe.add_compile_options({ "-Wall" });

  testing::internal::CaptureStdout();
  static_cast<void>(p.build(/*dry_run=*/true));
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_NE(out.find("mylib"), std::string::npos);
  EXPECT_NE(out.find("static library"), std::string::npos);
  EXPECT_NE(out.find("libmylib.a"), std::string::npos);
  EXPECT_NE(out.find("myapp"), std::string::npos);
  EXPECT_NE(out.find("executable"), std::string::npos);
  EXPECT_NE(out.find("-Wall"), std::string::npos);
  EXPECT_NE(out.find("links: mylib"), std::string::npos);
  EXPECT_NE(out.find(".ccbuild/obj/mylib/src/lib.o"), std::string::npos);
  EXPECT_NE(out.find(".ccbuild/obj/myapp/src/main.o"), std::string::npos);
}

TEST(ProjectTest, BuildOutputContainsIncludeDirs) {
  Project p("test");
  auto& exe = p.add_executable("app", { "main.cc" });
  exe.add_include_dirs({ "includes/pub" }, Visibility::Public);
  exe.add_include_dirs({ "includes/priv" }, Visibility::Private);

  testing::internal::CaptureStdout();
  static_cast<void>(p.build(/*dry_run=*/true));
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_NE(out.find("includes/pub"), std::string::npos);
  EXPECT_NE(out.find("includes/priv"), std::string::npos);
}

TEST(ProjectTest, BuildOutputContainsLinkOptions) {
  Project p("test");
  auto& exe = p.add_executable("app", { "main.cc" });
  exe.add_link_options({ "-lpthread", "-lz" });

  testing::internal::CaptureStdout();
  static_cast<void>(p.build(/*dry_run=*/true));
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_NE(out.find("link opts:"), std::string::npos);
  EXPECT_NE(out.find("-lpthread"), std::string::npos);
  EXPECT_NE(out.find("-lz"), std::string::npos);
}

TEST(ProjectTest, BuildOutputContainsPlanSummary) {
  Project p("test");
  p.add_library("lib", { "a.cc", "b.cc" });
  p.add_executable("app", { "main.cc" });

  testing::internal::CaptureStdout();
  static_cast<void>(p.build(/*dry_run=*/true));
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_NE(out.find("3 compile"), std::string::npos);
  EXPECT_NE(out.find("1 archive"), std::string::npos);
  EXPECT_NE(out.find("1 link"), std::string::npos);
}

// -- Building ------------------------------------------------------------

using ProjectBuildTest = TempDirTest;

TEST_F(ProjectBuildTest, NonDryRunBuildSucceeds) {
  write_file("main.cc", "int main() { return 0; }\n");

  Project p("test_project");
  p.add_executable("app", { "main.cc" });

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = p.build(/*dry_run=*/false);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/app"));
}

TEST_F(ProjectBuildTest, DryRunHasNoSideEffects) {
  Project p("test_project");
  p.add_executable("app", { "main.cc" });

  EXPECT_EQ(dry_run(p).rc, 0);
  EXPECT_FALSE(std::filesystem::exists(".ccbuild"));
}

TEST_F(ProjectBuildTest, ValidationFailureHasNoSideEffects) {
  Project p("test_project");
  p.add_executable("app", { "Makefile" });

  testing::internal::CaptureStderr();
  const int result = p.build(/*dry_run=*/false);
  testing::internal::GetCapturedStderr();

  EXPECT_NE(result, 0);
  EXPECT_FALSE(std::filesystem::exists(".ccbuild"));
}

}  // namespace
}  // namespace ccbuild
