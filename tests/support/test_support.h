#ifndef CCBUILD_TESTS_SUPPORT_TEST_SUPPORT_H
#define CCBUILD_TESTS_SUPPORT_TEST_SUPPORT_H

#include <gtest/gtest.h>
#include <stdlib.h>  // mkdtemp, setenv, unsetenv (POSIX)
#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

/// Helpers shared by the ccbuild test suites.
namespace ccbuild::test {

namespace fs = std::filesystem;

/// A fresh, uniquely named temporary directory, removed recursively on
/// destruction.
class ScopedTempDir {
 public:
  ScopedTempDir() {
    std::string pattern =
        (fs::temp_directory_path() / "ccbuild_test_XXXXXX").string();
    if (mkdtemp(pattern.data()) == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    path_ = pattern;
  }
  ~ScopedTempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  ScopedTempDir(const ScopedTempDir&) = delete;
  ScopedTempDir& operator=(const ScopedTempDir&) = delete;
  ScopedTempDir(ScopedTempDir&&) = delete;
  ScopedTempDir& operator=(ScopedTempDir&&) = delete;

  [[nodiscard]] const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

/// Changes the current working directory for its lifetime.
class ScopedCurrentDir {
 public:
  explicit ScopedCurrentDir(const fs::path& dir)
      : previous_(fs::current_path()) {
    fs::current_path(dir);
  }
  ~ScopedCurrentDir() {
    std::error_code ec;
    fs::current_path(previous_, ec);
  }
  ScopedCurrentDir(const ScopedCurrentDir&) = delete;
  ScopedCurrentDir& operator=(const ScopedCurrentDir&) = delete;
  ScopedCurrentDir(ScopedCurrentDir&&) = delete;
  ScopedCurrentDir& operator=(ScopedCurrentDir&&) = delete;

 private:
  fs::path previous_;
};

/// Sets (or, with std::nullopt, unsets) an environment variable for its
/// lifetime, restoring the previous value on destruction.
class ScopedEnv {
 public:
  ScopedEnv(std::string name, const std::optional<std::string>& value)
      : name_(std::move(name)) {
    if (const char* old = std::getenv(name_.c_str())) {
      previous_ = old;
    }
    apply(value);
  }
  ~ScopedEnv() { apply(previous_); }
  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;
  ScopedEnv(ScopedEnv&&) = delete;
  ScopedEnv& operator=(ScopedEnv&&) = delete;

 private:
  void apply(const std::optional<std::string>& value) const {
    if (value) {
      setenv(name_.c_str(), value->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  std::string name_;
  std::optional<std::string> previous_;
};

/// Write `contents` to `path`, creating parent directories as needed.
inline void write_file(const fs::path& path, std::string_view contents) {
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path());
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot write " + path.string());
  }
  out << contents;
}

/// Write an executable shell script to `path`.
inline void write_script(const fs::path& path, std::string_view body) {
  write_file(path, "#!/bin/sh\n" + std::string(body) + "\n");
  fs::permissions(path, fs::perms::owner_all | fs::perms::group_read |
                            fs::perms::group_exec | fs::perms::others_read |
                            fs::perms::others_exec);
}

/// Fixture for tests that touch the filesystem: each test runs with a
/// fresh, empty temporary directory as its working directory, so tests
/// never see each other's files and can run in parallel.
class TempDirTest : public ::testing::Test {
 protected:
  void SetUp() override { cwd_.emplace(dir_.path()); }
  void TearDown() override { cwd_.reset(); }

  [[nodiscard]] const fs::path& dir() const { return dir_.path(); }

 private:
  ScopedTempDir dir_;
  std::optional<ScopedCurrentDir> cwd_;
};

}  // namespace ccbuild::test

#endif  // CCBUILD_TESTS_SUPPORT_TEST_SUPPORT_H
