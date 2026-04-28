#include "ccbuild/static_library.h"

#include <gtest/gtest.h>

namespace ccbuild {
namespace {
TEST(StaticLibraryTest, KindIsStaticLibrary) {
  StaticLibrary lib("mylib", { "lib.cc" });
  EXPECT_EQ(lib.kind(), TargetKind::StaticLibrary);
}

TEST(StaticLibraryTest, OutputFilenameHasLibPrefix) {
  StaticLibrary lib("mylib", { "lib.cc" });
  EXPECT_EQ(lib.output_filename(), ".ccbuild/lib/libmylib.a");
}

}  // namespace
}  // namespace ccbuild