#include <gtest/gtest.h>

#include <array>
#include <list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"
#include "ccbuild/types.h"

namespace ccbuild {
namespace {

// -- Construction --------------------------------------------------------

TEST(TargetTest, NameIsPreserved) {
  Executable t("myapp", { "main.cc" });
  EXPECT_EQ(t.name(), "myapp");
}

TEST(TargetTest, SourceArePreserved) {
  Executable t("myapp", { "a.cc", "b.cc", "c.cc" });
  ASSERT_EQ(t.sources().size(), 3u);
  ASSERT_EQ(t.sources()[0], "a.cc");
  ASSERT_EQ(t.sources()[1], "b.cc");
  ASSERT_EQ(t.sources()[2], "c.cc");
}

TEST(TargetTest, EmptySourcesAllowed) {
  Executable t("myapp", {});
  EXPECT_TRUE(t.sources().empty());
}

// -- add_sources ---------------------------------------------------------

TEST(TargetTest, AddSourcesFromInitializerList) {
  Executable t("myapp", { "a.cc" });
  t.add_sources({ "b.cc", "c.cc" });
  ASSERT_EQ(t.sources().size(), 3u);
  EXPECT_EQ(t.sources()[2], "c.cc");
}

TEST(TargetTest, AddSourcesFromVector) {
  Executable t("myapp", {});
  std::vector<std::string> sources = { "a.cc", "b.cc", "c.cc" };
  t.add_sources(sources);
  ASSERT_EQ(t.sources().size(), 3u);
  EXPECT_EQ(t.sources()[0], "a.cc");
}

TEST(TargetTest, AddSourcesFromStringViews) {
  Executable t("myapp", {});
  const std::vector<std::string_view> sources = { "a.cc", "b.cc" };
  t.add_sources(sources);
  ASSERT_EQ(t.sources().size(), 2u);
  EXPECT_EQ(t.sources()[1], "b.cc");
}

TEST(TargetTest, AddSourcesFromCStringArray) {
  Executable t("myapp", {});
  constexpr std::array<const char*, 2> kSources = { "a.cc", "b.cc" };
  t.add_sources(kSources);
  EXPECT_EQ(t.sources().size(), 2u);
}

TEST(TargetTest, AddSourcesFromMovedVector) {
  Executable t("myapp", {});
  std::vector<std::string> sources = { "a.cc" };
  t.add_sources(std::move(sources));
  ASSERT_EQ(t.sources().size(), 1u);
  EXPECT_EQ(t.sources()[0], "a.cc");
}

TEST(TargetTest, AppendingATargetsOwnListIsSafe) {
  // The span returned by sources() aliases the vector being appended to.
  Executable t("myapp", {});
  for (int i = 0; i < 8; ++i) {
    t.add_sources({ "s" + std::to_string(i) + ".cc" });
  }
  t.add_sources(t.sources());
  ASSERT_EQ(t.sources().size(), 16u);
  EXPECT_EQ(t.sources()[8], "s0.cc");
  EXPECT_EQ(t.sources()[15], "s7.cc");

  t.add_compile_options({ "-Wall", "-O2" });
  t.add_compile_options(t.compile_options());
  EXPECT_EQ(t.compile_options().size(), 4u);
}

// -- StringRange concept -------------------------------------------------

static_assert(StringRange<std::vector<std::string>>);
static_assert(StringRange<const std::vector<std::string>&>);
static_assert(StringRange<std::vector<std::string_view>>);
static_assert(StringRange<std::list<std::string>>);
static_assert(StringRange<std::array<const char*, 3>>);
static_assert(!StringRange<std::vector<int>>);
static_assert(!StringRange<std::string>);  // a string is not a list of them
static_assert(SourceRange<std::vector<std::string>>);  // compatibility alias

// -- link ----------------------------------------------------------------

TEST(TargetTest, LinkDepsInitiallyEmpty) {
  Executable t("myapp", { "main.cc" });
  EXPECT_TRUE(t.link_deps().empty());
}

TEST(TargetTest, LinkAddsDependency) {
  Executable app("myapp", { "main.cc" });
  StaticLibrary lib("mylib", { "lib.cc" });
  app.link(lib);

  ASSERT_EQ(app.link_deps().size(), 1u);
  EXPECT_EQ(app.link_deps()[0].get().name(), "mylib");
}

TEST(TargetTest, LinkMultipleDeps) {
  Executable app("myapp", { "main.cc" });
  StaticLibrary a("a", { "a.cc" });
  StaticLibrary b("b", { "b.cc" });
  app.link(a).link(b);

  ASSERT_EQ(app.link_deps().size(), 2u);
  EXPECT_EQ(app.link_deps()[0].get().name(), "a");
  EXPECT_EQ(app.link_deps()[1].get().name(), "b");
}

// -- add_compile_options -------------------------------------------------

TEST(TargetTest, CompileOptionsInitiallyEmpty) {
  Executable app("myapp", { "main.cc" });
  EXPECT_TRUE(app.compile_options().empty());
}

TEST(TargetTest, AddCompileOptions) {
  Executable app("myapp", { "main.cc" });
  app.add_compile_options({ "-Wall", "-O2" });
  ASSERT_EQ(app.compile_options().size(), 2u);
  ASSERT_EQ(app.compile_options()[0], "-Wall");
  ASSERT_EQ(app.compile_options()[1], "-O2");
}

TEST(TargetTest, AddCompileOptionsAccumulates) {
  Executable app("myapp", { "main.cc" });
  app.add_compile_options({ "-Wall" });
  app.add_compile_options({ "-O2", "-g" });
  EXPECT_EQ(app.compile_options().size(), 3u);
}

TEST(TargetTest, AddCompileOptionsFromVector) {
  Executable app("myapp", { "main.cc" });
  const std::vector<std::string> opts = { "-Wall", "-O2" };
  app.add_compile_options(opts);
  ASSERT_EQ(app.compile_options().size(), 2u);
  EXPECT_EQ(app.compile_options()[1], "-O2");
}

// -- add_link_options ----------------------------------------------------

TEST(TargetTest, AddLinkOptions) {
  Executable app("myapp", { "main.cc" });
  app.add_link_options({ "-lpthread" });
  app.add_link_options(std::vector<std::string_view>{ "-ldl" });
  ASSERT_EQ(app.link_options().size(), 2u);
  EXPECT_EQ(app.link_options()[0], "-lpthread");
  EXPECT_EQ(app.link_options()[1], "-ldl");
}

// -- object_path ---------------------------------------------------------

TEST(TargetTest, ObjectPathSimpleFilename) {
  Executable app("myapp", { "main.cc" });
  EXPECT_EQ(app.object_path("main.cc"), ".ccbuild/obj/myapp/main.o");
}

TEST(TargetTest, ObjectPathWithDirectory) {
  Executable app("myapp", { "src/util.cc" });
  EXPECT_EQ(app.object_path("src/util.cc"), ".ccbuild/obj/myapp/src/util.o");
}

TEST(TargetTest, ObjectPathNestedDirectory) {
  Executable app("myapp", { "src/net/socket.cc" });
  EXPECT_EQ(app.object_path("src/net/socket.cc"),
            ".ccbuild/obj/myapp/src/net/socket.o");
}

TEST(TargetTest, ObjectPathCxxExtensions) {
  Executable app("myapp", { "io.cxx" });
  EXPECT_EQ(app.object_path("io.cxx"), ".ccbuild/obj/myapp/io.o");
}

TEST(TargetTest, ObjectPathCppExtension) {
  Executable app("myapp", { "main.cpp" });
  EXPECT_EQ(app.object_path("main.cpp"), ".ccbuild/obj/myapp/main.o");
}

TEST(TargetTest, ObjectPathParentDirectoryStaysInsideTargetDir) {
  Executable app("myapp", { "../shared/util.cc" });
  EXPECT_EQ(app.object_path("../shared/util.cc"),
            ".ccbuild/obj/myapp/__/shared/util.o");
  EXPECT_EQ(app.object_path("../../x.cc"), ".ccbuild/obj/myapp/__/__/x.o");
}

TEST(TargetTest, ObjectPathAbsoluteSourceIsMadeRelative) {
  Executable app("myapp", { "/opt/src/main.cc" });
  EXPECT_EQ(app.object_path("/opt/src/main.cc"),
            ".ccbuild/obj/myapp/opt/src/main.o");
}

TEST(TargetTest, ObjectPathIsNormalized) {
  Executable app("myapp", { "./src/../main.cc" });
  EXPECT_EQ(app.object_path("./src/../main.cc"), ".ccbuild/obj/myapp/main.o");
}

TEST(TargetTest, ObjectPathKeepsDotsInDirectoryNames) {
  Executable app("myapp", { "v1.2/main.cc" });
  EXPECT_EQ(app.object_path("v1.2/main.cc"), ".ccbuild/obj/myapp/v1.2/main.o");
}

TEST(TargetTest, ObjectPathCollisionDifferentDirs) {
  Executable app("myapp", { "src/net/socket.cc", "src/io/socket.cc" });
  auto netdir = app.object_path("src/net/socket.cc");
  auto iodir = app.object_path("src/io/socket.cc");
  EXPECT_NE(netdir, iodir);
  EXPECT_EQ(netdir, ".ccbuild/obj/myapp/src/net/socket.o");
  EXPECT_EQ(iodir, ".ccbuild/obj/myapp/src/io/socket.o");
}

// -- Fluent API chaining -------------------------------------------------

TEST(TargetTest, FluentChaining) {
  StaticLibrary dep("dep", { "dep.cc" });
  Executable t("myapp", { "main.cc" });

  auto& ref = t.link(dep).add_compile_options({ "-Wall" });
  EXPECT_EQ(&ref, &t);
  EXPECT_EQ(t.link_deps().size(), 1u);
  EXPECT_EQ(t.compile_options().size(), 1u);
}

// -- Include directories -------------------------------------------------

TEST(TargetTest, IncludeDirsInitiallyEmpty) {
  Executable t("myapp", { "main.cc" });
  EXPECT_TRUE(t.include_dirs(Visibility::Private).empty());
  EXPECT_TRUE(t.include_dirs(Visibility::Public).empty());
  EXPECT_TRUE(t.include_dirs(Visibility::Interface).empty());
}

TEST(TargetTest, AddIncludeDirsPrivate) {
  Executable t("myapp", { "main.cc" });
  t.add_include_dirs({ "src" }, Visibility::Private);
  ASSERT_EQ(t.include_dirs(Visibility::Private).size(), 1u);
  EXPECT_EQ(t.include_dirs(Visibility::Private)[0], "src");
  EXPECT_TRUE(t.include_dirs(Visibility::Public).empty());
}

TEST(TargetTest, AddIncludeDirsPublic) {
  Executable t("myapp", { "main.cc" });
  t.add_include_dirs({ "include" }, Visibility::Public);
  ASSERT_EQ(t.include_dirs(Visibility::Public).size(), 1u);
  EXPECT_EQ(t.include_dirs(Visibility::Public)[0], "include");
}

TEST(TargetTest, AddIncludeDirsInterface) {
  Executable t("myapp", { "main.cc" });
  t.add_include_dirs({ "api" }, Visibility::Interface);
  ASSERT_EQ(t.include_dirs(Visibility::Interface).size(), 1u);
  EXPECT_EQ(t.include_dirs(Visibility::Interface)[0], "api");
}

TEST(TargetTest, AddIncludeDirsAccumulates) {
  Executable t("myapp", { "main.cc" });
  t.add_include_dirs({ "a" }, Visibility::Private);
  t.add_include_dirs({ "b" }, Visibility::Private);
  EXPECT_EQ(t.include_dirs(Visibility::Private).size(), 2u);
}

TEST(TargetTest, AddIncludeDirsFromVector) {
  Executable t("myapp", { "main.cc" });
  const std::vector<std::string> dirs = { "a", "b" };
  t.add_include_dirs(dirs, Visibility::Public);
  EXPECT_EQ(t.include_dirs(Visibility::Public).size(), 2u);
  EXPECT_TRUE(t.include_dirs(Visibility::Private).empty());
}

TEST(TargetTest, AddIncludeDirsMultipleVisibilities) {
  Executable t("myapp", { "main.cc" });
  t.add_include_dirs({ "priv" }, Visibility::Private);
  t.add_include_dirs({ "pub" }, Visibility::Public);
  t.add_include_dirs({ "iface" }, Visibility::Interface);
  EXPECT_EQ(t.include_dirs(Visibility::Private).size(), 1u);
  EXPECT_EQ(t.include_dirs(Visibility::Public).size(), 1u);
  EXPECT_EQ(t.include_dirs(Visibility::Interface).size(), 1u);
}

// -- Link validation -----------------------------------------------------

TEST(TargetTest, LinkExeToExeThrows) {
  Executable a("a", { "a.cc" });
  Executable b("b", { "b.cc" });
  EXPECT_THROW(a.link(b), std::invalid_argument);
}

}  // namespace
}  // namespace ccbuild
