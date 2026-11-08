#include "internal/link_graph.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"

namespace ccbuild::internal {
namespace {

using Strings = std::vector<std::string>;

/// Names of the targets returned by link_order().
std::vector<std::string_view> names(const std::vector<const Target*>& targets) {
  std::vector<std::string_view> out;
  out.reserve(targets.size());
  for (const Target* t : targets) {
    out.push_back(t->name());
  }
  return out;
}

using Names = std::vector<std::string_view>;

// -- link_order ----------------------------------------------------------

TEST(LinkOrderTest, NoDependencies) {
  Executable app("app", { "main.cc" });
  EXPECT_TRUE(link_order(app).empty());
}

TEST(LinkOrderTest, IndependentLibrariesKeepDeclarationOrder) {
  StaticLibrary a("a", { "a.cc" });
  StaticLibrary b("b", { "b.cc" });
  Executable app("app", { "main.cc" });
  app.link(a).link(b);

  EXPECT_EQ(names(link_order(app)), (Names{ "a", "b" }));
}

TEST(LinkOrderTest, DependentsPrecedeTheirDependencies) {
  StaticLibrary base("base", { "base.cc" });
  StaticLibrary user("user", { "user.cc" });
  user.link(base);
  Executable app("app", { "main.cc" });
  app.link(base).link(user);  // declared in the "wrong" order

  EXPECT_EQ(names(link_order(app)), (Names{ "user", "base" }));
}

TEST(LinkOrderTest, SharedDependencyComesAfterAllUsers) {
  // app -> mid1 -> base, app -> mid2 -> base.  A naive depth-first order
  // (mid1, base, mid2) breaks static linking: symbols mid2 needs from base
  // are no longer resolved once base has been scanned.
  StaticLibrary base("base", { "base.cc" });
  StaticLibrary mid1("mid1", { "mid1.cc" });
  StaticLibrary mid2("mid2", { "mid2.cc" });
  mid1.link(base);
  mid2.link(base);
  Executable app("app", { "main.cc" });
  app.link(mid1).link(mid2);

  EXPECT_EQ(names(link_order(app)), (Names{ "mid1", "mid2", "base" }));
}

TEST(LinkOrderTest, EachLibraryAppearsOnce) {
  StaticLibrary base("base", { "base.cc" });
  StaticLibrary mid("mid", { "mid.cc" });
  mid.link(base).link(base);
  Executable app("app", { "main.cc" });
  app.link(mid).link(base).link(mid);

  EXPECT_EQ(names(link_order(app)), (Names{ "mid", "base" }));
}

TEST(LinkOrderTest, DeepChain) {
  StaticLibrary c("c", { "c.cc" });
  StaticLibrary b("b", { "b.cc" });
  StaticLibrary a("a", { "a.cc" });
  b.link(c);
  a.link(b);
  Executable app("app", { "main.cc" });
  app.link(a);

  EXPECT_EQ(names(link_order(app)), (Names{ "a", "b", "c" }));
}

TEST(LinkOrderTest, WorksForLibraries) {
  StaticLibrary base("base", { "base.cc" });
  StaticLibrary lib("lib", { "lib.cc" });
  lib.link(base);

  EXPECT_EQ(names(link_order(lib)), (Names{ "base" }));
}

// -- compile_include_dirs ------------------------------------------------

TEST(CompileIncludeDirsTest, OwnPrivateThenPublicExcludingInterface) {
  Executable app("app", { "main.cc" });
  app.add_include_dirs({ "pub" }, Visibility::Public);
  app.add_include_dirs({ "priv" }, Visibility::Private);
  app.add_include_dirs({ "iface" }, Visibility::Interface);

  EXPECT_EQ(compile_include_dirs(app), (Strings{ "priv", "pub" }));
}

TEST(CompileIncludeDirsTest, InheritsPublicAndInterfaceTransitively) {
  StaticLibrary base("base", { "base.cc" });
  base.add_include_dirs({ "base/pub" }, Visibility::Public);
  base.add_include_dirs({ "base/priv" }, Visibility::Private);
  base.add_include_dirs({ "base/iface" }, Visibility::Interface);

  StaticLibrary mid("mid", { "mid.cc" });
  mid.add_include_dirs({ "mid/pub" }, Visibility::Public);
  mid.link(base);

  Executable app("app", { "main.cc" });
  app.add_include_dirs({ "app" }, Visibility::Private);
  app.link(mid);

  EXPECT_EQ(compile_include_dirs(app),
            (Strings{ "app", "mid/pub", "base/pub", "base/iface" }));
}

TEST(CompileIncludeDirsTest, RemovesDuplicates) {
  StaticLibrary a("a", { "a.cc" });
  a.add_include_dirs({ "shared", "a" }, Visibility::Public);
  StaticLibrary b("b", { "b.cc" });
  b.add_include_dirs({ "shared", "b" }, Visibility::Public);

  Executable app("app", { "main.cc" });
  app.add_include_dirs({ "shared" }, Visibility::Private);
  app.link(a).link(b);

  EXPECT_EQ(compile_include_dirs(app), (Strings{ "shared", "a", "b" }));
}

// -- link_flags ----------------------------------------------------------

TEST(LinkFlagsTest, OwnFlagsThenLibraryFlagsInLinkOrder) {
  StaticLibrary base("base", { "base.cc" });
  base.add_link_options({ "-lbase_sys" });
  StaticLibrary mid("mid", { "mid.cc" });
  mid.add_link_options({ "-lmid_sys" });
  mid.link(base);

  Executable app("app", { "main.cc" });
  app.add_link_options({ "-static" });
  app.link(mid);

  EXPECT_EQ(link_flags(app), (Strings{ "-static", "-lmid_sys", "-lbase_sys" }));
}

TEST(LinkFlagsTest, NoFlags) {
  Executable app("app", { "main.cc" });
  EXPECT_TRUE(link_flags(app).empty());
}

}  // namespace
}  // namespace ccbuild::internal
