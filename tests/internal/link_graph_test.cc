#include "internal/link_graph.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"

namespace ccbuild::internal {
namespace {

using Strings = std::vector<std::string>;

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

}  // namespace
}  // namespace ccbuild::internal
