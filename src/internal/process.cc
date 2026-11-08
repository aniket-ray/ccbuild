#include "internal/process.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

extern char** environ;  // NOLINT(readability-redundant-declaration)

namespace ccbuild::internal {

namespace {

/// Exit code reported (in shell convention) for a program that never ran.
constexpr int kCommandNotFoundExitCode = 127;

/// Offset added to a signal number to form a shell-style exit code.
constexpr int kSignalExitBase = 128;

/// Search path used when $PATH is unset.
constexpr std::string_view kDefaultPath = "/usr/local/bin:/usr/bin:/bin";

// `sigaction` and `stat` each name both a function and a struct; these
// aliases name the structs.
using SigAction = struct sigaction;
using FileStat = struct stat;

/// An argument vector in the form posix_spawn expects: a null-terminated
/// array of mutable C strings.  Owns copies of the arguments.
class Argv {
 public:
  explicit Argv(std::span<const std::string> args)
      : storage_(args.begin(), args.end()) {
    pointers_.reserve(storage_.size() + 1);
    for (auto& arg : storage_) {
      pointers_.push_back(arg.data());
    }
    pointers_.push_back(nullptr);
  }

  [[nodiscard]] const char* file() const { return storage_.front().c_str(); }
  [[nodiscard]] char* const* data() { return pointers_.data(); }

 private:
  std::vector<std::string> storage_;
  std::vector<char*> pointers_;
};

/// RAII wrapper for posix_spawn_file_actions_t.
class FileActions {
 public:
  FileActions() { posix_spawn_file_actions_init(&actions_); }
  ~FileActions() { posix_spawn_file_actions_destroy(&actions_); }
  FileActions(const FileActions&) = delete;
  FileActions& operator=(const FileActions&) = delete;
  FileActions(FileActions&&) = delete;
  FileActions& operator=(FileActions&&) = delete;

  [[nodiscard]] posix_spawn_file_actions_t* get() { return &actions_; }

 private:
  posix_spawn_file_actions_t actions_{};
};

/// RAII wrapper for posix_spawnattr_t.
class SpawnAttributes {
 public:
  SpawnAttributes() { posix_spawnattr_init(&attr_); }
  ~SpawnAttributes() { posix_spawnattr_destroy(&attr_); }
  SpawnAttributes(const SpawnAttributes&) = delete;
  SpawnAttributes& operator=(const SpawnAttributes&) = delete;
  SpawnAttributes(SpawnAttributes&&) = delete;
  SpawnAttributes& operator=(SpawnAttributes&&) = delete;

  /// Give the child the default action for every signal in `signals`.
  void set_default_signals(const sigset_t& signals) {
    posix_spawnattr_setsigdefault(&attr_, &signals);
    posix_spawnattr_setflags(&attr_, POSIX_SPAWN_SETSIGDEF);
  }

  [[nodiscard]] posix_spawnattr_t* get() { return &attr_; }

 private:
  posix_spawnattr_t attr_{};
};

/// Ignores SIGINT and SIGQUIT for its lifetime, restoring the previous
/// dispositions on destruction.
class ScopedIgnoreInterrupts {
 public:
  ScopedIgnoreInterrupts() {
    SigAction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGINT, &ignore, &old_int_);
    sigaction(SIGQUIT, &ignore, &old_quit_);
  }
  ~ScopedIgnoreInterrupts() {
    sigaction(SIGINT, &old_int_, nullptr);
    sigaction(SIGQUIT, &old_quit_, nullptr);
  }
  ScopedIgnoreInterrupts(const ScopedIgnoreInterrupts&) = delete;
  ScopedIgnoreInterrupts& operator=(const ScopedIgnoreInterrupts&) = delete;
  ScopedIgnoreInterrupts(ScopedIgnoreInterrupts&&) = delete;
  ScopedIgnoreInterrupts& operator=(ScopedIgnoreInterrupts&&) = delete;

  /// The interrupt signals that were not ignored before this guard took
  /// effect.  A child should get the default action for exactly these:
  /// that is what it would have inherited across exec without the guard.
  [[nodiscard]] sigset_t previously_not_ignored() const {
    sigset_t signals;
    sigemptyset(&signals);
    if (old_int_.sa_handler != SIG_IGN) {
      sigaddset(&signals, SIGINT);
    }
    if (old_quit_.sa_handler != SIG_IGN) {
      sigaddset(&signals, SIGQUIT);
    }
    return signals;
  }

 private:
  SigAction old_int_{};
  SigAction old_quit_{};
};

/// Owns a file descriptor and closes it on destruction.
class UniqueFd {
 public:
  explicit UniqueFd(int fd) : fd_(fd) {}
  ~UniqueFd() { reset(); }
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;
  UniqueFd(UniqueFd&&) = delete;
  UniqueFd& operator=(UniqueFd&&) = delete;

  [[nodiscard]] int get() const { return fd_; }
  void reset() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_;
};

/// Wait for `pid` to terminate, retrying on EINTR, and decode its status.
ProcessStatus wait_for(pid_t pid) {
  int raw = 0;
  while (waitpid(pid, &raw, 0) < 0) {
    if (errno != EINTR) {
      return { ProcessStatus::Kind::kFailedToStart, errno };
    }
  }
  if (WIFSIGNALED(raw)) {
    return { ProcessStatus::Kind::kSignaled, WTERMSIG(raw) };
  }
  if (WIFEXITED(raw)) {
    return { ProcessStatus::Kind::kExited, WEXITSTATUS(raw) };
  }
  return { ProcessStatus::Kind::kExited, 1 };
}

bool is_shell_safe(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return true;
  }
  static constexpr std::string_view kSafePunctuation = "_+-./=:,@%";
  return kSafePunctuation.find(c) != std::string_view::npos;
}

bool is_executable_file(const std::string& path) {
  FileStat st{};
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
         access(path.c_str(), X_OK) == 0;
}

}  // namespace

// -- ProcessStatus -------------------------------------------------------

int ProcessStatus::shell_exit_code() const {
  switch (kind) {
  case Kind::kExited:
    return code;
  case Kind::kSignaled:
    return kSignalExitBase + code;
  case Kind::kFailedToStart:
    return kCommandNotFoundExitCode;
  }
  return 1;
}

std::string ProcessStatus::describe() const {
  switch (kind) {
  case Kind::kExited:
    return "exited with code " + std::to_string(code);
  case Kind::kSignaled:
    return "killed by signal " + std::to_string(code) + " (" + strsignal(code) +
           ")";
  case Kind::kFailedToStart:
    return std::string("could not be started: ") + std::strerror(code);
  }
  return "unknown status";
}

// -- Running processes ---------------------------------------------------

ProcessStatus run_process(std::span<const std::string> argv) {
  if (argv.empty()) {
    return { ProcessStatus::Kind::kFailedToStart, EINVAL };
  }
  Argv args(argv);

  const ScopedIgnoreInterrupts ignore_interrupts;
  SpawnAttributes attr;
  attr.set_default_signals(ignore_interrupts.previously_not_ignored());

  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, args.file(), nullptr, attr.get(),
                              args.data(), environ);
  if (rc != 0) {
    return { ProcessStatus::Kind::kFailedToStart, rc };
  }
  return wait_for(pid);
}

CapturedOutput capture_process(std::span<const std::string> argv,
                               bool merge_stderr) {
  CapturedOutput result;
  if (argv.empty()) {
    result.status = { ProcessStatus::Kind::kFailedToStart, EINVAL };
    return result;
  }

  std::array<int, 2> fds{};
  if (pipe(fds.data()) != 0) {
    result.status = { ProcessStatus::Kind::kFailedToStart, errno };
    return result;
  }
  const UniqueFd read_end(fds[0]);
  UniqueFd write_end(fds[1]);
  // Keep the pipe itself out of the child; the dup2'd copies survive exec.
  fcntl(read_end.get(), F_SETFD, FD_CLOEXEC);
  fcntl(write_end.get(), F_SETFD, FD_CLOEXEC);

  FileActions actions;
  posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null",
                                   O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(actions.get(), write_end.get(),
                                   STDOUT_FILENO);
  if (merge_stderr) {
    posix_spawn_file_actions_adddup2(actions.get(), write_end.get(),
                                     STDERR_FILENO);
  }

  Argv args(argv);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, args.file(), actions.get(), nullptr,
                              args.data(), environ);
  write_end.reset();  // so read() sees EOF once the child exits
  if (rc != 0) {
    result.status = { ProcessStatus::Kind::kFailedToStart, rc };
    return result;
  }

  static constexpr std::size_t kChunkSize = 4096;
  std::array<char, kChunkSize> buf{};
  for (;;) {
    const ssize_t n = read(read_end.get(), buf.data(), buf.size());
    if (n > 0) {
      result.output.append(buf.data(), static_cast<std::size_t>(n));
    } else if (n == 0 || errno != EINTR) {
      break;
    }
  }

  result.status = wait_for(pid);
  return result;
}

// -- Locating programs ---------------------------------------------------

std::optional<std::string> find_executable(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  if (name.find('/') != std::string_view::npos) {
    std::string path(name);
    if (is_executable_file(path)) {
      return path;
    }
    return std::nullopt;
  }

  const char* env_path = std::getenv("PATH");
  const std::string_view search =
      (env_path != nullptr) ? std::string_view(env_path) : kDefaultPath;

  std::size_t begin = 0;
  while (begin <= search.size()) {
    std::size_t end = search.find(':', begin);
    if (end == std::string_view::npos) {
      end = search.size();
    }
    const std::string_view dir = search.substr(begin, end - begin);
    if (!dir.empty()) {
      std::string candidate(dir);
      if (candidate.back() != '/') {
        candidate += '/';
      }
      candidate += name;
      if (is_executable_file(candidate)) {
        return candidate;
      }
    }
    begin = end + 1;
  }
  return std::nullopt;
}

// -- Formatting ----------------------------------------------------------

std::string shell_quote(std::string_view arg) {
  if (!arg.empty() && std::ranges::all_of(arg, is_shell_safe)) {
    return std::string(arg);
  }
  std::string quoted = "'";
  for (const char c : arg) {
    if (c == '\'') {
      quoted += "'\\''";  // close quote, escaped quote, reopen quote
    } else {
      quoted += c;
    }
  }
  quoted += '\'';
  return quoted;
}

std::string format_command(std::span<const std::string> argv) {
  std::string command;
  for (const auto& arg : argv) {
    if (!command.empty()) {
      command += ' ';
    }
    command += shell_quote(arg);
  }
  return command;
}

}  // namespace ccbuild::internal
