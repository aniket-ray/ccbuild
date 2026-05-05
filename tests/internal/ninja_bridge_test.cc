#include "internal/ninja_bridge.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "ccbuild/project.h"

namespace ccbuild {
namespace internal {
namespace {

// -- RAII helpers ---------------------------------------------------------

struct TempFile {
  std::string path;
  explicit TempFile(std::string p) : path(std::move(p)) {}
  ~TempFile() { std::filesystem::remove(path); }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
};

struct TempDir {
  std::filesystem::path path;
  explicit TempDir(std::filesystem::path p) : path(std::move(p)) {
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
};

// -- Fixture --------------------------------------------------------------

class NinjaBridgeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::filesystem::remove_all(".ccbuild");

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(".", ec)) {
      if (ec)
        break;
      const auto& p = entry.path();
      const auto name = p.filename().string();
      bool is_test_file = (name.find("_nb_test_") == 0) ||
                          (name == "ninja_bridge_test_dummy.cc") ||
                          (name == "ninja_bridge_test_lib.cc") ||
                          (name == "ninja_bridge_test_main.cc");
      if (is_test_file) {
        std::filesystem::remove_all(p, ec);
      }
      ec.clear();
    }
  }

  void TearDown() override { std::filesystem::remove_all(".ccbuild"); }
};

TEST_F(NinjaBridgeTest, EmptyProjectSucceeds) {
  const Project p("empty_project");

  testing::internal::CaptureStdout();
  const int result = NinjaBridge::build(p);
  const std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(result, 0);
  EXPECT_NE(out.find("nothing to do"), std::string::npos);
}

TEST_F(NinjaBridgeTest, BuildFailsOnMissingSource) {
  Project p("missing_src_project");
  p.add_executable("app", { "does_not_exist.cc" });

  testing::internal::CaptureStderr();
  testing::internal::CaptureStdout();
  const int result = NinjaBridge::build(p);
  const std::string out = testing::internal::GetCapturedStdout();
  const std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(result, 0);
  bool has_error = (out.find("error") != std::string::npos) ||
                   (err.find("error") != std::string::npos);
  EXPECT_TRUE(has_error);
}

TEST_F(NinjaBridgeTest, BuildSucceedsWithValidSource) {
  const std::string src_file = "ninja_bridge_test_dummy.cc";

  {
    std::ofstream f(src_file);
    ASSERT_TRUE(f.is_open());
    f << "int main() { return 0; }\n";
  }

  Project p("dummy_project");
  p.add_executable("dummy_app", { src_file });

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/dummy_app"));
}

TEST_F(NinjaBridgeTest, BuildSucceedsWithComplexGraph) {
  const std::string lib_src = "ninja_bridge_test_lib.cc";
  const std::string main_src = "ninja_bridge_test_main.cc";

  {
    std::ofstream f(lib_src);
    ASSERT_TRUE(f.is_open());
    f << "int the_answer() { return 42; }\n";
  }
  {
    std::ofstream f(main_src);
    ASSERT_TRUE(f.is_open());
    f << "int the_answer();\n"
         "int main() { return the_answer() == 42 ? 0 : 1; }\n";
  }

  Project p("complex_project");

  auto& lib = p.add_library("my_lib", { lib_src });
  lib.add_include_dirs({ "include" }, Visibility::Public);
  lib.add_include_dirs({ "src" }, Visibility::Private);
  lib.add_compile_options({ "-DTEST_DEFINE" });

  auto& app = p.add_executable("my_app", { main_src });
  app.link(lib);
  app.add_link_options({ "-O0" });
  app.add_include_dirs({ "app_include" }, Visibility::Interface);

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/my_app"));
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/lib/libmy_lib.a"));
}

TEST_F(NinjaBridgeTest, LibraryWithLinkOptionsBuildsCorrectly) {
  TempFile src("_nb_test_ar_src.cc");
  {
    std::ofstream f(src.path);
    f << "int func() { return 1; }\n";
  }

  Project p("ar_test");
  auto& lib = p.add_library("my_lib", { src.path });
  lib.add_link_options({ "-s" });

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/lib/libmy_lib.a"));
}

TEST_F(NinjaBridgeTest, InterfaceIncludeDirsPropagateToLinker) {
  TempDir inc("_nb_test_iface_inc");
  TempFile lib_src("_nb_test_iface_lib.cc");
  TempFile exe_src("_nb_test_iface_exe.cc");

  {
    std::ofstream f((inc.path / "util.h").string());
    f << "#pragma once\ninline int util() { return 99; }\n";
  }
  {
    std::ofstream f(lib_src.path);
    f << "int lib_func() { return 1; }\n";
  }
  {
    std::ofstream f(exe_src.path);
    f << "#include \"util.h\"\n"
         "int lib_func();\n"
         "int main() { return lib_func() + util() == 100 ? 0 : 1; }\n";
  }

  Project p("iface_test");
  auto& lib = p.add_library("mylib", { lib_src.path });
  lib.add_include_dirs({ inc.path.string() }, Visibility::Interface);
  auto& app = p.add_executable("myapp", { exe_src.path });
  app.link(lib);

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/myapp"));
}

TEST_F(NinjaBridgeTest, SecondBuildAlreadyUpToDate) {
  TempFile src("_nb_test_uptodate.cc");
  {
    std::ofstream f(src.path);
    f << "int main() { return 0; }\n";
  }

  Project p("uptodate_test");
  p.add_executable("app", { src.path });

  {
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    const int result = NinjaBridge::build(p);
    testing::internal::GetCapturedStdout();
    testing::internal::GetCapturedStderr();
    EXPECT_EQ(result, 0);
  }

  testing::internal::CaptureStdout();
  const int result = NinjaBridge::build(p);
  const std::string out = testing::internal::GetCapturedStdout();

  EXPECT_EQ(result, 0);
  EXPECT_NE(out.find("nothing to do"), std::string::npos);
}

TEST_F(NinjaBridgeTest, BuildFailsOnCompilationError) {
  TempFile src("_nb_test_comperr.cc");
  {
    std::ofstream f(src.path);
    f << "int main() { return 1 }\n";
  }

  Project p("comperr_test");
  p.add_executable("app", { src.path });

  testing::internal::CaptureStderr();
  testing::internal::CaptureStdout();
  const int result = NinjaBridge::build(p);
  const std::string out = testing::internal::GetCapturedStdout();
  const std::string err = testing::internal::GetCapturedStderr();

  EXPECT_NE(result, 0);
  bool has_error = (out.find("error") != std::string::npos) ||
                   (err.find("error") != std::string::npos);
  EXPECT_TRUE(has_error);
}

TEST_F(NinjaBridgeTest, SourceInSubdirectoryBuildsCorrectObject) {
  TempDir src_dir("_nb_test_src");
  TempFile src((src_dir.path / "main.cc").string());

  {
    std::ofstream f(src.path);
    f << "int main() { return 0; }\n";
  }

  Project p("subdir_test");
  p.add_executable("app", { src.path });

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/obj/app/_nb_test_src/main.o"));
}

TEST_F(NinjaBridgeTest, MultiDepthTransitiveIncludes) {
  TempDir inc_a("_nb_test_inc_a");
  TempDir inc_b("_nb_test_inc_b");
  TempFile liba_src("_nb_test_trans_a.cc");
  TempFile libb_src("_nb_test_trans_b.cc");
  TempFile exe_src("_nb_test_trans_exe.cc");

  {
    std::ofstream f((inc_a.path / "a.h").string());
    f << "#pragma once\nint func_a();\n";
  }
  {
    std::ofstream f((inc_b.path / "b.h").string());
    f << "#pragma once\nint func_b();\n";
  }
  {
    std::ofstream f(liba_src.path);
    f << "int func_a() { return 1; }\n";
  }
  {
    std::ofstream f(libb_src.path);
    f << "#include \"a.h\"\n"
         "#include \"b.h\"\n"
         "int func_b() { return func_a() + 1; }\n";
  }
  {
    std::ofstream f(exe_src.path);
    f << "#include \"b.h\"\n"
         "int main() { return func_b() == 2 ? 0 : 1; }\n";
  }

  Project p("trans_test");
  auto& liba = p.add_library("liba", { liba_src.path });
  liba.add_include_dirs({ inc_a.path.string() }, Visibility::Public);

  auto& libb = p.add_library("libb", { libb_src.path });
  libb.add_include_dirs({ inc_b.path.string() }, Visibility::Public);
  libb.link(liba);

  auto& app = p.add_executable("app", { exe_src.path });
  app.link(libb);

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/app"));
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/lib/libliba.a"));
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/lib/liblibb.a"));
}

TEST_F(NinjaBridgeTest, TwoExecutablesBuildBoth) {
  TempFile src1("_nb_test_two_1.cc");
  TempFile src2("_nb_test_two_2.cc");

  {
    std::ofstream f(src1.path);
    f << "int main() { return 0; }\n";
  }
  {
    std::ofstream f(src2.path);
    f << "int main() { return 1; }\n";
  }

  Project p("two_exe_test");
  p.add_executable("app1", { src1.path });
  p.add_executable("app2", { src2.path });

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/app1"));
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/app2"));
}

TEST_F(NinjaBridgeTest, MultipleLinkOptionsSeparation) {
  TempFile src("_nb_test_mlo.cc");
  {
    std::ofstream f(src.path);
    f << "int main() { return 0; }\n";
  }

  Project p("mlo_test");
  auto& app = p.add_executable("app", { src.path });
  app.add_link_options({ "-s", "-v" });

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/app"));
}

TEST_F(NinjaBridgeTest, CompileOptionsWithoutIncludeDirs) {
  TempFile src("_nb_test_co.cc");
  {
    std::ofstream f(src.path);
    f << "int main() { return 0; }\n";
  }

  Project p("co_test");
  auto& app = p.add_executable("app", { src.path });
  app.add_compile_options({ "-Wall", "-Wextra" });

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/app"));
}

TEST_F(NinjaBridgeTest, DuplicateTransitiveLinkDeps) {
  TempFile base_src("_nb_test_dup_base.cc");
  TempFile mid1_src("_nb_test_dup_mid1.cc");
  TempFile mid2_src("_nb_test_dup_mid2.cc");
  TempFile exe_src("_nb_test_dup_exe.cc");

  {
    std::ofstream f(base_src.path);
    f << "int base_func() { return 1; }\n";
  }
  {
    std::ofstream f(mid1_src.path);
    f << "int base_func();\n"
         "int mid1_func() { return base_func() + 1; }\n";
  }
  {
    std::ofstream f(mid2_src.path);
    f << "int base_func();\n"
         "int mid2_func() { return base_func() + 2; }\n";
  }
  {
    std::ofstream f(exe_src.path);
    f << "int mid1_func();\n"
         "int mid2_func();\n"
         "int main() { return (mid1_func() + mid2_func()) == 5 ? 0 : 1; }\n";
  }

  Project p("dup_deps_test");
  auto& base = p.add_library("base", { base_src.path });
  auto& mid1 = p.add_library("mid1", { mid1_src.path });
  auto& mid2 = p.add_library("mid2", { mid2_src.path });
  mid1.link(base);
  mid2.link(base);
  auto& app = p.add_executable("app", { exe_src.path });
  app.link(mid1).link(mid2);

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  const int result = NinjaBridge::build(p);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(".ccbuild/bin/app"));
}

}  // namespace
}  // namespace internal
}  // namespace ccbuild
