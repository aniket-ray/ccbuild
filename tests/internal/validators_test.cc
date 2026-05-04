#include "internal/validators.h"

#include <gtest/gtest.h>

#include "internal/compiler.h"

namespace ccbuild {
namespace {

// -- CompilerInfo::kind_str ----------------------------------------------

TEST(CompilerInfoTest, KindStrMapping) {
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Gcc }).kind_str(), "GCC");
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Clang }).kind_str(), "Clang");
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Unknown }).kind_str(),
            "Unknown");
}

// -- detect_compiler -----------------------------------------------------

TEST(DetectCompilerTest, FindsDefaultCompiler) {
  unsetenv("CXX");
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  EXPECT_FALSE(info->path.empty());
  EXPECT_FALSE(info->version.empty());
  EXPECT_GT(info->major_version, 0);
  EXPECT_TRUE(info->kind == CompilerKind::Gcc ||
              info->kind == CompilerKind::Clang);
}

TEST(DetectCompilerTest, RespectsEnvCXX) {
  setenv("CXX", "g++", 1);
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
#ifdef __APPLE__
  EXPECT_EQ(info->kind, CompilerKind::Clang);
#else
  EXPECT_EQ(info->kind, CompilerKind::Gcc);
#endif
  unsetenv("CXX");
}

TEST(DetectCompilerTest, InvalidCXXFallsThrough) {
  setenv("CXX", "/nonexistent/compiler", 1);
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(info->kind == CompilerKind::Gcc ||
              info->kind == CompilerKind::Clang);
  unsetenv("CXX");
}

TEST(DetectCompilerTest, VersionHasThreeComponents) {
  unsetenv("CXX");
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
  unsetenv("CXX");
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  auto dot_pos = info->version.find('.');
  ASSERT_NE(dot_pos, std::string::npos);
  int expected_major = std::stoi(info->version.substr(0, dot_pos));
  EXPECT_EQ(info->major_version, expected_major);
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

}  // namespace
}  // namespace ccbuild
