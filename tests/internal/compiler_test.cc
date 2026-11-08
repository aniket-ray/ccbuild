#include "internal/compiler.h"

#include <gtest/gtest.h>

#include <string>

#include "support/test_support.h"

namespace ccbuild::internal {
namespace {

using test::ScopedEnv;
using test::TempDirTest;
using test::write_script;

// -- identify_kind -------------------------------------------------------

TEST(IdentifyKindTest, DetectsClang) {
  EXPECT_EQ(identify_kind("clang version 15.0.0"), CompilerKind::Clang);
  EXPECT_EQ(identify_kind("Apple clang version 14.0.3 (clang-1403.0.22.14.1)"),
            CompilerKind::Clang);
}

TEST(IdentifyKindTest, DetectsGpp) {
  EXPECT_EQ(identify_kind("g++ (GCC) 13.2.0"), CompilerKind::Gcc);
  EXPECT_EQ(identify_kind("g++ (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0"),
            CompilerKind::Gcc);
}

TEST(IdentifyKindTest, DetectsGCC) {
  EXPECT_EQ(identify_kind("gcc (GCC) 13.2.1 20230728 (Red Hat 13.2.1-1)"),
            CompilerKind::Gcc);
  EXPECT_EQ(identify_kind("GCC 12.2.0"), CompilerKind::Gcc);
}

TEST(IdentifyKindTest, UnknownForEmpty) {
  EXPECT_EQ(identify_kind(""), CompilerKind::Unknown);
}

TEST(IdentifyKindTest, UnknownForUnrecognized) {
  EXPECT_EQ(identify_kind("SomeRandomCompiler version 1.0"),
            CompilerKind::Unknown);
}

TEST(IdentifyKindTest, ClangBeatsGcc) {
  EXPECT_EQ(identify_kind("clang with gcc compatibility"), CompilerKind::Clang);
}

TEST(IdentifyKindTest, DetectsLowercaseGcc) {
  EXPECT_EQ(identify_kind("gcc version 4.8.5"), CompilerKind::Gcc);
  EXPECT_EQ(identify_kind("SomeTool based on gcc 9.3.0"), CompilerKind::Gcc);
}

// -- extract_version -----------------------------------------------------

TEST(ExtractVersionTest, ExtractsStandardVersion) {
  EXPECT_EQ(extract_version("g++ (GCC) 11.5.0"), "11.5.0");
  EXPECT_EQ(extract_version("clang version 15.0.0 (git-hash)"), "15.0.0");
}

TEST(ExtractVersionTest, NoVersionReturnsEmpty) {
  EXPECT_TRUE(extract_version("no version here").empty());
}

TEST(ExtractVersionTest, EmptyInputReturnsEmpty) {
  EXPECT_TRUE(extract_version("").empty());
}

TEST(ExtractVersionTest, FirstMatchWins) {
  EXPECT_EQ(extract_version("version 1.2.3 and 4.5.6"), "1.2.3");
}

// -- extract_major -------------------------------------------------------

TEST(ExtractMajorTest, ExtractsNormalVersion) {
  EXPECT_EQ(extract_major("11.5.0"), 11);
  EXPECT_EQ(extract_major("15.0.0"), 15);
}

TEST(ExtractMajorTest, EmptyReturnsZero) {
  EXPECT_EQ(extract_major(""), 0);
}

TEST(ExtractMajorTest, NonNumericReturnsZero) {
  EXPECT_EQ(extract_major("abc"), 0);
  EXPECT_EQ(extract_major("not.a.number"), 0);
}

TEST(ExtractMajorTest, SingleDigitVersion) {
  EXPECT_EQ(extract_major("7.0.0"), 7);
}

TEST(ExtractMajorTest, LeadingGarbageReturnsZero) {
  EXPECT_EQ(extract_major(".5.0"), 0);
}

// -- CompilerInfo::kind_str ----------------------------------------------

TEST(CompilerInfoTest, KindStrMapping) {
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Gcc }).kind_str(), "GCC");
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Clang }).kind_str(), "Clang");
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Unknown }).kind_str(),
            "Unknown");
}

// -- probe_compiler ------------------------------------------------------

using ProbeCompilerTest = TempDirTest;

TEST_F(ProbeCompilerTest, ParsesVersionBanner) {
  write_script("fakecxx", "echo 'fakecxx (GCC) 12.3.4'");

  auto info = probe_compiler("./fakecxx");
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->path, "./fakecxx");
  EXPECT_EQ(info->kind, CompilerKind::Gcc);
  EXPECT_EQ(info->version, "12.3.4");
  EXPECT_EQ(info->major_version, 12);
}

TEST_F(ProbeCompilerTest, BareNameIsNotLookedUpInCurrentDirectory) {
  write_script("fakecxx", "echo 'fakecxx (GCC) 12.3.4'");
  const ScopedEnv path("PATH", (dir() / "empty-bin").string());

  EXPECT_FALSE(probe_compiler("fakecxx").has_value());
}

TEST_F(ProbeCompilerTest, BareNameIsLookedUpOnPath) {
  write_script("bin/fakecxx", "echo 'clang version 18.1.0'");
  const ScopedEnv path("PATH", (dir() / "bin").string());

  auto info = probe_compiler("fakecxx");
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->path, (dir() / "bin" / "fakecxx").string());
  EXPECT_EQ(info->kind, CompilerKind::Clang);
}

TEST_F(ProbeCompilerTest, FailingVersionCommandIsRejected) {
  write_script("brokencxx", "echo 'g++ 1.2.3'; exit 1");
  EXPECT_FALSE(probe_compiler("./brokencxx").has_value());
}

TEST_F(ProbeCompilerTest, SilentVersionCommandIsRejected) {
  write_script("silentcxx", "exit 0");
  EXPECT_FALSE(probe_compiler("./silentcxx").has_value());
}

TEST(ProbeCompilerMissingTest, MissingCompilerReturnsNullopt) {
  EXPECT_FALSE(probe_compiler("/nonexistent/compiler").has_value());
  EXPECT_FALSE(probe_compiler("").has_value());
}

// -- detect_compiler -----------------------------------------------------

TEST(DetectCompilerTest, FindsDefaultCompiler) {
  const ScopedEnv cxx("CXX", std::nullopt);
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  EXPECT_FALSE(info->path.empty());
  EXPECT_FALSE(info->version.empty());
  EXPECT_GT(info->major_version, 0);
  EXPECT_TRUE(info->kind == CompilerKind::Gcc ||
              info->kind == CompilerKind::Clang);
}

TEST(DetectCompilerTest, RespectsEnvCXX) {
  const ScopedEnv cxx("CXX", "g++");
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
#ifdef __APPLE__
  EXPECT_EQ(info->kind, CompilerKind::Clang);
#else
  EXPECT_EQ(info->kind, CompilerKind::Gcc);
#endif
}

TEST(DetectCompilerTest, InvalidCXXFallsThroughWithWarning) {
  const ScopedEnv cxx("CXX", "/nonexistent/compiler");

  testing::internal::CaptureStderr();
  auto info = detect_compiler();
  const std::string err = testing::internal::GetCapturedStderr();

  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(info->kind == CompilerKind::Gcc ||
              info->kind == CompilerKind::Clang);
  EXPECT_NE(err.find("is not a usable compiler"), std::string::npos);
}

TEST(DetectCompilerTest, EmptyCXXIsIgnoredSilently) {
  const ScopedEnv cxx("CXX", "");

  testing::internal::CaptureStderr();
  auto info = detect_compiler();
  const std::string err = testing::internal::GetCapturedStderr();

  EXPECT_TRUE(info.has_value());
  EXPECT_TRUE(err.empty());
}

TEST(DetectCompilerTest, VersionHasThreeComponents) {
  const ScopedEnv cxx("CXX", std::nullopt);
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  int dots = 0;
  for (char c : info->version) {
    if (c == '.') {
      ++dots;
    }
  }
  EXPECT_EQ(dots, 2);
}

TEST(DetectCompilerTest, MajorVersionMatchesVersionString) {
  const ScopedEnv cxx("CXX", std::nullopt);
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  auto dot_pos = info->version.find('.');
  ASSERT_NE(dot_pos, std::string::npos);
  int expected_major = std::stoi(info->version.substr(0, dot_pos));
  EXPECT_EQ(info->major_version, expected_major);
}

}  // namespace
}  // namespace ccbuild::internal
