#include "ccbuild/executable.h"

#include <gtest/gtest.h>

namespace ccbuild {
namespace {

// -- Target Kind ---------------------------------------------------------

TEST(ExecutableTest, KindIsExecutable) {
  Executable exe("myexe", { "main.cc" });
  EXPECT_EQ(exe.kind(), TargetKind::Executable);
}

// -- Output Filename -----------------------------------------------------

TEST(ExecutableTest, OutputFilenameMatchesName) {
  Executable exe("myexe", { "main.cc" });
  EXPECT_EQ(exe.output_filename(), ".ccbuild/bin/myexe");
}

}  // namespace
}  // namespace ccbuild
