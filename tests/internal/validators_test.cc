#include <gtest/gtest.h>

#include "internal/compiler.h"

namespace ccbuild {
namespace {

TEST(CompilerInfoTest, KindStrMapping) {
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::GCC }).kind_str(), "GCC");
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Clang }).kind_str(), "Clang");
  EXPECT_EQ((CompilerInfo{ .kind = CompilerKind::Unknown }).kind_str(),
            "Unknown");
}

TEST(DetectCompilerTest, FindsDefaultCompiler) {
  unsetenv("CXX");
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  EXPECT_FALSE(info->path.empty());
  EXPECT_FALSE(info->version.empty());
  EXPECT_GT(info->major_version, 0);
  EXPECT_TRUE(info->kind == CompilerKind::GCC ||
              info->kind == CompilerKind::Clang);
}

TEST(DetectCompilerTest, RespectsEnvCXX) {
  setenv("CXX", "g++", 1);
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
#ifdef __APPLE__
  EXPECT_EQ(info->kind, CompilerKind::Clang);
#else
  EXPECT_EQ(info->kind, CompilerKind::GCC);
#endif
  unsetenv("CXX");
}

TEST(DetectCompilerTest, InvalidCXXFallsThrough) {
  setenv("CXX", "/nonexistent/compiler", 1);
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(info->kind == CompilerKind::GCC ||
              info->kind == CompilerKind::Clang);
  unsetenv("CXX");
}

TEST(DetectCompilerTest, VersionHasThreeComponents) {
  unsetenv("CXX");
  auto info = detect_compiler();
  ASSERT_TRUE(info.has_value());
  int dots = 0;
  for (char c : info->version)
    if (c == '.')
      dots++;
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

}  // namespace
}  // namespace ccbuild
