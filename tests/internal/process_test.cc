#include "internal/process.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <csignal>
#include <string>
#include <vector>

#include "support/test_support.h"

namespace ccbuild::internal {
namespace {

using test::ScopedEnv;
using test::TempDirTest;
using test::write_file;
using test::write_script;

using Args = std::vector<std::string>;

// `sigaction` names both a function and a struct; this names the struct.
using SigAction = struct sigaction;

// -- ProcessStatus -------------------------------------------------------

TEST(ProcessStatusTest, ShellExitCodes) {
  EXPECT_EQ(
      (ProcessStatus{ ProcessStatus::Kind::kExited, 3 }).shell_exit_code(), 3);
  EXPECT_EQ((ProcessStatus{ ProcessStatus::Kind::kSignaled, SIGKILL })
                .shell_exit_code(),
            128 + SIGKILL);
  EXPECT_EQ((ProcessStatus{ ProcessStatus::Kind::kFailedToStart, ENOENT })
                .shell_exit_code(),
            127);
}

TEST(ProcessStatusTest, SuccessOnlyForExitZero) {
  EXPECT_TRUE((ProcessStatus{ ProcessStatus::Kind::kExited, 0 }).success());
  EXPECT_FALSE((ProcessStatus{ ProcessStatus::Kind::kExited, 1 }).success());
  EXPECT_FALSE((ProcessStatus{ ProcessStatus::Kind::kSignaled, 0 }).success());
  EXPECT_FALSE(
      (ProcessStatus{ ProcessStatus::Kind::kFailedToStart, 0 }).success());
}

TEST(ProcessStatusTest, Describe) {
  EXPECT_EQ((ProcessStatus{ ProcessStatus::Kind::kExited, 2 }).describe(),
            "exited with code 2");
  EXPECT_NE((ProcessStatus{ ProcessStatus::Kind::kSignaled, SIGTERM })
                .describe()
                .find("killed by signal 15"),
            std::string::npos);
  EXPECT_NE((ProcessStatus{ ProcessStatus::Kind::kFailedToStart, ENOENT })
                .describe()
                .find("could not be started"),
            std::string::npos);
}

// -- run_process ---------------------------------------------------------

TEST(RunProcessTest, ReportsExitCode) {
  EXPECT_TRUE(run_process(Args{ "true" }).success());

  const auto status = run_process(Args{ "sh", "-c", "exit 3" });
  EXPECT_EQ(status.kind, ProcessStatus::Kind::kExited);
  EXPECT_EQ(status.code, 3);
}

TEST(RunProcessTest, ReportsSignal) {
  const auto status = run_process(Args{ "sh", "-c", "kill -TERM $$" });
  EXPECT_EQ(status.kind, ProcessStatus::Kind::kSignaled);
  EXPECT_EQ(status.code, SIGTERM);
  EXPECT_EQ(status.shell_exit_code(), 128 + SIGTERM);
}

TEST(RunProcessTest, MissingProgramFailsToStart) {
  const auto status = run_process(Args{ "/nonexistent/program" });
  EXPECT_EQ(status.kind, ProcessStatus::Kind::kFailedToStart);
  EXPECT_EQ(status.code, ENOENT);
  EXPECT_EQ(status.shell_exit_code(), 127);
}

TEST(RunProcessTest, EmptyArgvFailsToStart) {
  const auto status = run_process(Args{});
  EXPECT_EQ(status.kind, ProcessStatus::Kind::kFailedToStart);
  EXPECT_EQ(status.code, EINVAL);
}

TEST(RunProcessTest, PassesArgumentsVerbatimWithoutAShell) {
  // Spaces, quotes and shell metacharacters must arrive unchanged.
  const std::string tricky = "a b\"c'$(echo no);*";
  const auto status = run_process(
      Args{ "sh", "-c", "test \"$1\" = \"$2\"", "sh", tricky, tricky });
  EXPECT_TRUE(status.success());
}

// The child must receive SIGINT with its default action (so Ctrl-C stops
// it) even though run_process ignores SIGINT in the parent while waiting.
TEST(RunProcessTest, ChildGetsDefaultInterruptHandling) {
  SigAction dfl{};
  dfl.sa_handler = SIG_DFL;
  sigemptyset(&dfl.sa_mask);
  SigAction saved{};
  sigaction(SIGINT, &dfl, &saved);

  const auto status = run_process(Args{ "sh", "-c", "kill -INT $$" });

  sigaction(SIGINT, &saved, nullptr);
  EXPECT_EQ(status.kind, ProcessStatus::Kind::kSignaled);
  EXPECT_EQ(status.code, SIGINT);
}

TEST(RunProcessTest, RestoresInterruptDisposition) {
  SigAction before{};
  sigaction(SIGINT, nullptr, &before);

  static_cast<void>(run_process(Args{ "true" }));

  SigAction after{};
  sigaction(SIGINT, nullptr, &after);
  EXPECT_EQ(before.sa_handler, after.sa_handler);
}

// -- capture_process -----------------------------------------------------

TEST(CaptureProcessTest, CapturesStdout) {
  const auto result = capture_process(Args{ "echo", "hello world" });
  EXPECT_TRUE(result.status.success());
  EXPECT_EQ(result.output, "hello world\n");
}

TEST(CaptureProcessTest, MergesStderrByDefault) {
  const auto result =
      capture_process(Args{ "sh", "-c", "echo out; echo err >&2" });
  EXPECT_NE(result.output.find("out"), std::string::npos);
  EXPECT_NE(result.output.find("err"), std::string::npos);
}

TEST(CaptureProcessTest, CanLeaveStderrAlone) {
  testing::internal::CaptureStderr();
  const auto result = capture_process(
      Args{ "sh", "-c", "echo out; echo err >&2" }, /*merge_stderr=*/false);
  const std::string err = testing::internal::GetCapturedStderr();
  EXPECT_EQ(result.output, "out\n");
  EXPECT_EQ(err, "err\n");
}

TEST(CaptureProcessTest, StdinIsEmpty) {
  // `cat` would block forever if stdin were inherited from a terminal.
  const auto result = capture_process(Args{ "cat" });
  EXPECT_TRUE(result.status.success());
  EXPECT_TRUE(result.output.empty());
}

TEST(CaptureProcessTest, CapturesOutputLargerThanPipeBuffer) {
  const auto result =
      capture_process(Args{ "sh", "-c", "yes x | head -c 300000" });
  EXPECT_TRUE(result.status.success());
  EXPECT_EQ(result.output.size(), 300000u);
}

TEST(CaptureProcessTest, ReportsExitCode) {
  const auto result = capture_process(Args{ "sh", "-c", "echo bye; exit 4" });
  EXPECT_EQ(result.status.code, 4);
  EXPECT_EQ(result.output, "bye\n");
}

TEST(CaptureProcessTest, MissingProgramFailsToStart) {
  const auto result = capture_process(Args{ "/nonexistent/program" });
  EXPECT_EQ(result.status.kind, ProcessStatus::Kind::kFailedToStart);
  EXPECT_TRUE(result.output.empty());
}

// -- find_executable -----------------------------------------------------

using FindExecutableTest = TempDirTest;

TEST_F(FindExecutableTest, FindsProgramOnPath) {
  write_script("bin/tool", "true");
  const ScopedEnv path("PATH", "/nonexistent:" + (dir() / "bin").string());

  EXPECT_EQ(find_executable("tool"), (dir() / "bin" / "tool").string());
}

TEST_F(FindExecutableTest, FirstMatchOnPathWins) {
  write_script("first/tool", "true");
  write_script("second/tool", "true");
  const ScopedEnv path(
      "PATH", (dir() / "first").string() + ":" + (dir() / "second").string());

  EXPECT_EQ(find_executable("tool"), (dir() / "first" / "tool").string());
}

TEST_F(FindExecutableTest, SkipsEmptyPathEntries) {
  // An empty PATH entry conventionally means ".", which would let a file in
  // the project directory shadow a real tool.  ccbuild ignores it.
  write_script("tool", "true");
  const ScopedEnv path("PATH", ":" + (dir() / "empty").string() + ":");

  EXPECT_FALSE(find_executable("tool").has_value());
}

TEST_F(FindExecutableTest, SkipsNonExecutableFilesAndDirectories) {
  write_file("bin/tool", "not executable");
  std::filesystem::create_directories("bin/dir");
  const ScopedEnv path("PATH", (dir() / "bin").string());

  EXPECT_FALSE(find_executable("tool").has_value());
  EXPECT_FALSE(find_executable("dir").has_value());
}

TEST_F(FindExecutableTest, PathWithSlashIsCheckedDirectly) {
  write_script("tool", "true");
  EXPECT_EQ(find_executable("./tool"), "./tool");
  EXPECT_FALSE(find_executable("./missing").has_value());
}

TEST(FindExecutableBasicTest, EmptyNameIsNotFound) {
  EXPECT_FALSE(find_executable("").has_value());
}

TEST(FindExecutableBasicTest, FindsShell) {
  const auto sh = find_executable("sh");
  ASSERT_TRUE(sh.has_value());
  EXPECT_TRUE(sh->ends_with("/sh"));
}

// -- shell_quote / format_command ----------------------------------------

TEST(ShellQuoteTest, LeavesSafeArgumentsAlone) {
  EXPECT_EQ(shell_quote("abc"), "abc");
  EXPECT_EQ(shell_quote("-std=c++20"), "-std=c++20");
  EXPECT_EQ(shell_quote("/usr/bin/g++"), "/usr/bin/g++");
  EXPECT_EQ(shell_quote("-Wl,-rpath,/opt/lib"), "-Wl,-rpath,/opt/lib");
}

TEST(ShellQuoteTest, QuotesUnsafeArguments) {
  EXPECT_EQ(shell_quote(""), "''");
  EXPECT_EQ(shell_quote("a b"), "'a b'");
  EXPECT_EQ(shell_quote("$(rm -rf ~)"), "'$(rm -rf ~)'");
  EXPECT_EQ(shell_quote("-DMSG=\"hi\""), "'-DMSG=\"hi\"'");
  EXPECT_EQ(shell_quote("it's"), "'it'\\''s'");
}

TEST(ShellQuoteTest, RoundTripsThroughShell) {
  for (const std::string arg :
       { "a b", "it's", "\"q\"", "$HOME", "`x`", "a\\b", "*", "~", "" }) {
    const auto result =
        capture_process(Args{ "sh", "-c", "printf '%s' " + shell_quote(arg) });
    EXPECT_EQ(result.output, arg) << "argument: " << arg;
  }
}

TEST(FormatCommandTest, JoinsQuotedArguments) {
  EXPECT_EQ(format_command(Args{ "g++", "-I my dir", "x.cc" }),
            "g++ '-I my dir' x.cc");
  EXPECT_EQ(format_command(Args{}), "");
}

}  // namespace
}  // namespace ccbuild::internal
