#include "internal/compiler.h"

#include <gtest/gtest.h>

namespace ccbuild {
namespace internal {
namespace {

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

}  // namespace
}  // namespace internal
}  // namespace ccbuild
