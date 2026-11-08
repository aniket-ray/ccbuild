#ifndef CCBUILD_INTERNAL_PROCESS_H
#define CCBUILD_INTERNAL_PROCESS_H

#include <optional>
#include <span>
#include <string>
#include <string_view>

/// Subprocess execution without a shell.
///
/// Every external program ccbuild runs directly (the host compiler, the
/// build-script runner, `--version` probes) goes through this module.
/// Commands are argument vectors passed to posix_spawn, never strings handed
/// to /bin/sh, so paths and arguments need no quoting and cannot be
/// misinterpreted.  (Ninja runs the per-file compile and link commands
/// itself, through the shell; see ninja_bridge.cc and shell_quote().)
namespace ccbuild::internal {

/// How a child process ended.
struct ProcessStatus {
  enum class Kind {
    kExited,         ///< Exited normally; `code` is the exit code.
    kSignaled,       ///< Killed by a signal; `code` is the signal number.
    kFailedToStart,  ///< Never ran; `code` is the errno from spawning.
  };

  Kind kind = Kind::kFailedToStart;
  int code = 0;

  /// True if the process exited normally with exit code 0.
  [[nodiscard]] bool success() const {
    return kind == Kind::kExited && code == 0;
  }

  /// The status in shell convention: the exit code, 128 + signal number if
  /// killed by a signal, or 127 if the program could not be started.
  [[nodiscard]] int shell_exit_code() const;

  /// Human-readable description, e.g. "exited with code 1",
  /// "killed by signal 9 (Killed)" or "could not be started: No such file".
  [[nodiscard]] std::string describe() const;
};

/// Output captured from a process, plus how it ended.
struct CapturedOutput {
  ProcessStatus status;
  std::string output;
};

/// Run a program and wait for it, with stdin/stdout/stderr inherited.
///
/// `argv[0]` is looked up on PATH unless it contains a '/'.  While the child
/// runs, SIGINT and SIGQUIT are ignored by this process (as system(3)
/// does), so Ctrl-C is handled by the child, which can clean up and report
/// its own status.
///
/// @pre !argv.empty()
[[nodiscard]] ProcessStatus run_process(std::span<const std::string> argv);

/// Run a program and capture its stdout (and stderr, if `merge_stderr`).
/// stdin is redirected from /dev/null.
///
/// @pre !argv.empty()
[[nodiscard]] CapturedOutput capture_process(std::span<const std::string> argv,
                                             bool merge_stderr = true);

/// Locate an executable file.
///
/// A name containing '/' is checked as-is.  Otherwise each directory in
/// $PATH is searched in order (empty entries are skipped rather than
/// treated as the current directory).  Returns the path of the first
/// executable regular file found.
[[nodiscard]] std::optional<std::string> find_executable(std::string_view name);

/// Quote one argument for POSIX sh.  Arguments made only of characters that
/// are never special to the shell are returned unchanged; anything else is
/// wrapped in single quotes.
[[nodiscard]] std::string shell_quote(std::string_view arg);

/// Render an argument vector as a shell command line, for display.
[[nodiscard]] std::string format_command(std::span<const std::string> argv);

}  // namespace ccbuild::internal

#endif  // CCBUILD_INTERNAL_PROCESS_H
