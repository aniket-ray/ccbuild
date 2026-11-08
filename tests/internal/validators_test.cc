#include "internal/validators.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"

namespace ccbuild {
namespace {

namespace validators = internal::validators;

using Targets = std::vector<std::unique_ptr<Target>>;

Executable& add_exe(Targets& targets, std::string name,
                    std::vector<std::string> sources) {
  auto t = std::make_unique<Executable>(name, std::move(sources));
  auto& ref = *t;
  targets.push_back(std::move(t));
  return ref;
}

StaticLibrary& add_lib(Targets& targets, std::string name,
                       std::vector<std::string> sources) {
  auto t = std::make_unique<StaticLibrary>(name, std::move(sources));
  auto& ref = *t;
  targets.push_back(std::move(t));
  return ref;
}

// -- is_cpp_source -------------------------------------------------------

TEST(IsCppSourceTest, RejectsNoExtension) {
  EXPECT_FALSE(validators::is_cpp_source("Makefile"));
  EXPECT_FALSE(validators::is_cpp_source("README"));
}

TEST(IsCppSourceTest, AcceptsAllExtensions) {
  EXPECT_TRUE(validators::is_cpp_source("file.cc"));
  EXPECT_TRUE(validators::is_cpp_source("file.cpp"));
  EXPECT_TRUE(validators::is_cpp_source("file.cxx"));
  EXPECT_TRUE(validators::is_cpp_source("file.c++"));
  EXPECT_TRUE(validators::is_cpp_source("file.c"));
  EXPECT_TRUE(validators::is_cpp_source("file.C"));
}

TEST(IsCppSourceTest, RejectsNonCppExtension) {
  EXPECT_FALSE(validators::is_cpp_source("file.h"));
  EXPECT_FALSE(validators::is_cpp_source("file.hpp"));
  EXPECT_FALSE(validators::is_cpp_source("file.txt"));
}

TEST(IsCppSourceTest, RejectsDotfile) {
  EXPECT_FALSE(validators::is_cpp_source(".hidden"));
}

// -- check_duplicate_target_names ----------------------------------------

TEST(CheckDuplicateTargetNamesTest, UniqueNamesReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<Executable>("a", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<Executable>("b", std::vector<std::string>{ "b.cc" }));

  auto err = validators::check_duplicate_target_names(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckDuplicateTargetNamesTest, DuplicateNameReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<Executable>("foo", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<Executable>("foo", std::vector<std::string>{ "b.cc" }));

  auto err = validators::check_duplicate_target_names(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("duplicate target name"), std::string::npos);
  EXPECT_NE(err->find("foo"), std::string::npos);
}

TEST(CheckDuplicateTargetNamesTest, EmptyListReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;

  auto err = validators::check_duplicate_target_names(targets);
  EXPECT_FALSE(err.has_value());
}

// -- check_targets_have_sources ------------------------------------------

TEST(CheckTargetsHaveSourcesTest, AllHaveSourcesReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(std::make_unique<Executable>(
      "app", std::vector<std::string>{ "main.cc" }));

  auto err = validators::check_targets_have_sources(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckTargetsHaveSourcesTest, EmptySourcesReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<Executable>("app", std::vector<std::string>{}));

  auto err = validators::check_targets_have_sources(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("no sources"), std::string::npos);
  EXPECT_NE(err->find("app"), std::string::npos);
}

TEST(CheckTargetsHaveSourcesTest, OneEmptyAmongManyReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<Executable>("a", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<Executable>("b", std::vector<std::string>{}));

  auto err = validators::check_targets_have_sources(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("b"), std::string::npos);
}

// -- check_source_extensions ---------------------------------------------

TEST(CheckSourceExtensionsTest, AllValidReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(std::make_unique<Executable>(
      "app", std::vector<std::string>{ "main.cc" }));

  auto err = validators::check_source_extensions(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckSourceExtensionsTest, InvalidExtensionReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(std::make_unique<Executable>(
      "app", std::vector<std::string>{ "Makefile" }));

  auto err = validators::check_source_extensions(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("invalid source file"), std::string::npos);
  EXPECT_NE(err->find("Makefile"), std::string::npos);
}

TEST(CheckSourceExtensionsTest, AllSixExtensionsPass) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(std::make_unique<Executable>(
      "app", std::vector<std::string>{ "a.cc", "b.cpp", "c.cxx", "d.c++", "e.c",
                                       "f.C" }));

  auto err = validators::check_source_extensions(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckSourceExtensionsTest, OneInvalidAmongValidReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(std::make_unique<Executable>(
      "app", std::vector<std::string>{ "good.cc", "bad.txt" }));

  auto err = validators::check_source_extensions(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("bad.txt"), std::string::npos);
}

// -- check_no_exe_links_exe ----------------------------------------------

TEST(CheckNoExeLinksExeTest, ExeLinksLibReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(std::make_unique<Executable>(
      "app", std::vector<std::string>{ "main.cc" }));
  targets.push_back(std::make_unique<StaticLibrary>(
      "lib", std::vector<std::string>{ "lib.cc" }));
  static_cast<Executable&>(*targets[0]).link(*targets[1]);

  auto err = validators::check_no_exe_links_exe(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckNoExeLinksExeTest, LibLinksLibReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<StaticLibrary>("a", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("b", std::vector<std::string>{ "b.cc" }));
  static_cast<StaticLibrary&>(*targets[0]).link(*targets[1]);

  auto err = validators::check_no_exe_links_exe(targets);
  EXPECT_FALSE(err.has_value());
}

// -- check_no_link_cycles ------------------------------------------------

TEST(CheckNoLinkCyclesTest, NoDepsReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(std::make_unique<Executable>(
      "app", std::vector<std::string>{ "main.cc" }));

  auto err = validators::check_no_link_cycles(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckNoLinkCyclesTest, SimpleChainReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<StaticLibrary>("a", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("b", std::vector<std::string>{ "b.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("c", std::vector<std::string>{ "c.cc" }));
  static_cast<StaticLibrary&>(*targets[1]).link(*targets[0]);
  static_cast<StaticLibrary&>(*targets[2]).link(*targets[1]);

  auto err = validators::check_no_link_cycles(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckNoLinkCyclesTest, DiamondDagReturnsNullopt) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<StaticLibrary>("d", std::vector<std::string>{ "d.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("b", std::vector<std::string>{ "b.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("c", std::vector<std::string>{ "c.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("a", std::vector<std::string>{ "a.cc" }));
  // a → b, a → c, b → d, c → d
  static_cast<StaticLibrary&>(*targets[3]).link(*targets[1]);
  static_cast<StaticLibrary&>(*targets[3]).link(*targets[2]);
  static_cast<StaticLibrary&>(*targets[1]).link(*targets[0]);
  static_cast<StaticLibrary&>(*targets[2]).link(*targets[0]);

  auto err = validators::check_no_link_cycles(targets);
  EXPECT_FALSE(err.has_value());
}

TEST(CheckNoLinkCyclesTest, DirectCycleReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<StaticLibrary>("a", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("b", std::vector<std::string>{ "b.cc" }));
  static_cast<StaticLibrary&>(*targets[0]).link(*targets[1]);
  static_cast<StaticLibrary&>(*targets[1]).link(*targets[0]);

  auto err = validators::check_no_link_cycles(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("link cycle"), std::string::npos);
}

TEST(CheckNoLinkCyclesTest, SelfLoopReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<StaticLibrary>("a", std::vector<std::string>{ "a.cc" }));
  static_cast<StaticLibrary&>(*targets[0]).link(*targets[0]);

  auto err = validators::check_no_link_cycles(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("link cycle"), std::string::npos);
}

TEST(CheckNoLinkCyclesTest, TransitiveCycleReturnsError) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<StaticLibrary>("a", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("b", std::vector<std::string>{ "b.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("c", std::vector<std::string>{ "c.cc" }));
  static_cast<StaticLibrary&>(*targets[0]).link(*targets[1]);
  static_cast<StaticLibrary&>(*targets[1]).link(*targets[2]);
  static_cast<StaticLibrary&>(*targets[2]).link(*targets[0]);

  auto err = validators::check_no_link_cycles(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("link cycle"), std::string::npos);
}

TEST(CheckNoLinkCyclesTest, RecursiveVisitBeforeOuterLoop) {
  std::vector<std::unique_ptr<Target>> targets;
  targets.push_back(
      std::make_unique<StaticLibrary>("c", std::vector<std::string>{ "c.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("a", std::vector<std::string>{ "a.cc" }));
  targets.push_back(
      std::make_unique<StaticLibrary>("b", std::vector<std::string>{ "b.cc" }));
  static_cast<StaticLibrary&>(*targets[1]).link(*targets[2]);

  auto err = validators::check_no_link_cycles(targets);
  EXPECT_FALSE(err.has_value());
}

// -- is_valid_target_name -----------------------------------------------

TEST(IsValidTargetNameTest, AcceptsCMakeStyleNames) {
  for (const char* name : { "app", "my_lib", "lib-2", "a.b", "c++", "X9" }) {
    EXPECT_TRUE(validators::is_valid_target_name(name)) << name;
  }
}

TEST(IsValidTargetNameTest, RejectsPathsAndSpecialNames) {
  for (const char* name :
       { "", ".", "..", "a/b", "../x", "a b", "a\\b", "a\tb", "a$b", "a;b" }) {
    EXPECT_FALSE(validators::is_valid_target_name(name)) << name;
  }
}

TEST(CheckTargetNamesTest, InvalidNameReturnsError) {
  Targets targets;
  add_exe(targets, "ok", { "a.cc" });
  add_exe(targets, "not/ok", { "b.cc" });

  auto err = validators::check_target_names(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("invalid target name 'not/ok'"), std::string::npos);
}

// -- check_cxx_standard --------------------------------------------------

TEST(CheckCxxStandardTest, SupportedStandards) {
  for (const int standard : { 98, 11, 14, 17, 20, 23, 26 }) {
    EXPECT_TRUE(validators::is_supported_cxx_standard(standard)) << standard;
    EXPECT_FALSE(validators::check_cxx_standard(standard).has_value());
  }
}

TEST(CheckCxxStandardTest, UnsupportedStandards) {
  for (const int standard : { 0, 3, 21, 2020, -17 }) {
    auto err = validators::check_cxx_standard(standard);
    ASSERT_TRUE(err.has_value()) << standard;
    EXPECT_NE(err->find("unsupported C++ standard"), std::string::npos);
  }
}

// -- check_unique_object_paths -------------------------------------------

TEST(CheckUniqueObjectPathsTest, DistinctSourcesReturnNullopt) {
  Targets targets;
  add_exe(targets, "app", { "a.cc", "src/a.cc", "b.cpp" });
  EXPECT_FALSE(validators::check_unique_object_paths(targets).has_value());
}

TEST(CheckUniqueObjectPathsTest, SameSourceInDifferentTargetsIsFine) {
  Targets targets;
  add_exe(targets, "app1", { "main.cc" });
  add_exe(targets, "app2", { "main.cc" });
  EXPECT_FALSE(validators::check_unique_object_paths(targets).has_value());
}

TEST(CheckUniqueObjectPathsTest, RepeatedSourceReturnsError) {
  Targets targets;
  add_exe(targets, "app", { "a.cc", "a.cc" });

  auto err = validators::check_unique_object_paths(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("lists source 'a.cc' more than once"), std::string::npos);
}

TEST(CheckUniqueObjectPathsTest, ExtensionCollisionReturnsError) {
  Targets targets;
  add_exe(targets, "app", { "a.cc", "a.cpp" });

  auto err = validators::check_unique_object_paths(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("'a.cc' and 'a.cpp'"), std::string::npos);
}

TEST(CheckUniqueObjectPathsTest, EquivalentSpellingsCollide) {
  Targets targets;
  add_exe(targets, "app", { "src/a.cc", "./src/a.cc" });
  EXPECT_TRUE(validators::check_unique_object_paths(targets).has_value());
}

// -- check_link_deps_in_project ------------------------------------------

TEST(CheckLinkDepsInProjectTest, OwnedDepsReturnNullopt) {
  Targets targets;
  auto& lib = add_lib(targets, "lib", { "lib.cc" });
  add_exe(targets, "app", { "main.cc" }).link(lib);
  EXPECT_FALSE(validators::check_link_deps_in_project(targets).has_value());
}

TEST(CheckLinkDepsInProjectTest, ForeignDepReturnsError) {
  StaticLibrary foreign("foreign", { "f.cc" });
  Targets targets;
  add_exe(targets, "app", { "main.cc" }).link(foreign);

  auto err = validators::check_link_deps_in_project(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("'app' links 'foreign'"), std::string::npos);
}

// -- check_no_link_cycles: error message ---------------------------------

TEST(CheckNoLinkCyclesTest, ErrorNamesTheCycle) {
  Targets targets;
  auto& root = add_lib(targets, "root", { "r.cc" });
  auto& a = add_lib(targets, "a", { "a.cc" });
  auto& b = add_lib(targets, "b", { "b.cc" });
  root.link(a);
  a.link(b);
  b.link(a);

  auto err = validators::check_no_link_cycles(targets);
  ASSERT_TRUE(err.has_value());
  // Only the cycle itself is reported, not the path leading to it.
  EXPECT_EQ(*err, "link cycle: a -> b -> a");
}

TEST(CheckNoLinkCyclesTest, SelfLoopMessage) {
  Targets targets;
  auto& a = add_lib(targets, "a", { "a.cc" });
  a.link(a);

  auto err = validators::check_no_link_cycles(targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, "link cycle: a -> a");
}

// -- check_all -----------------------------------------------------------

TEST(CheckAllTest, ValidProjectReturnsNullopt) {
  Targets targets;
  auto& lib = add_lib(targets, "lib", { "lib.cc" });
  add_exe(targets, "app", { "main.cc" }).link(lib);
  EXPECT_FALSE(validators::check_all(20, targets).has_value());
}

TEST(CheckAllTest, ReportsFirstFailingCheck) {
  Targets targets;
  add_exe(targets, "a b", {});  // invalid name and no sources

  auto err = validators::check_all(20, targets);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("invalid target name"), std::string::npos);
}

TEST(CheckAllTest, ChecksStandard) {
  Targets targets;
  add_exe(targets, "app", { "main.cc" });
  EXPECT_TRUE(validators::check_all(19, targets).has_value());
}

}  // namespace
}  // namespace ccbuild
