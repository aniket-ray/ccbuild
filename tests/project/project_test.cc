#include "ccbuild/project.h"

#include <gtest/gtest.h>

#include <string>

#include "ccbuild/executable.h"

namespace ccbuild {
namespace {

TEST(ProjectTest, DefaultStandardIs17) {
  Project p("myproject");
  EXPECT_EQ(p.standard(), 17);
}

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
  EXPECT_NE(err.find("link cycle"), std::string::npos);
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

TEST(ProjectTest, BuildOutputContainsProjectName) {
  Project p("coolproject");
  p.set_cxx_standard(20);
  p.add_executable("app", { "main.cc" });

  testing::internal::CaptureStdout();
  (void)p.build(/*dry_run=*/true);
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
  (void)p.build(/*dry_run=*/true);
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

TEST(ProjectTest, BuildOutputContainsPlanSummary) {
  Project p("test");
  p.add_library("lib", { "a.cc", "b.cc" });
  p.add_executable("app", { "main.cc" });

  testing::internal::CaptureStdout();
  (void)p.build(/*dry_run=*/true);
  std::string out = testing::internal::GetCapturedStdout();

  EXPECT_NE(out.find("3 compile"), std::string::npos);
  EXPECT_NE(out.find("1 archive"), std::string::npos);
  EXPECT_NE(out.find("1 link"), std::string::npos);
}

}  // namespace
}  // namespace ccbuild
