#include "internal/validators.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ccbuild/executable.h"
#include "ccbuild/static_library.h"

namespace ccbuild {
namespace {

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

}  // namespace
}  // namespace ccbuild
