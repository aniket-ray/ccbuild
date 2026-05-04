/// ccbuild's self-hosting build script.
///
/// This is the canonical build.cc that ccbuild uses to build itself.
/// After the initial CMake bootstrap, running `ccbuild` in the repo root
/// compiles and executes this script via the cached runner.
///
/// Targets:
///   - ninjacore:  static library from the ninja submodule
///   - ccbuildlib: static library from ccbuild's own source
///   - ccbuild:    CLI executable linking both libraries

#include <ccbuild/ccbuild.h>

#include <thread>

int main() {
  ccbuild::Project p("ccbuild");
  p.set_cxx_standard(20);

  // -- ninjacore: Ninja build engine (static library) --------------------
  auto& ninjacore =
      p.add_library("ninjacore", {
                                     "ninja/src/build_log.cc",
                                     "ninja/src/build.cc",
                                     "ninja/src/clean.cc",
                                     "ninja/src/clparser.cc",
                                     "ninja/src/debug_flags.cc",
                                     "ninja/src/depfile_parser.cc",
                                     "ninja/src/deps_log.cc",
                                     "ninja/src/disk_interface.cc",
                                     "ninja/src/dyndep.cc",
                                     "ninja/src/dyndep_parser.cc",
                                     "ninja/src/edit_distance.cc",
                                     "ninja/src/elide_middle.cc",
                                     "ninja/src/eval_env.cc",
                                     "ninja/src/explanations.cc",
                                     "ninja/src/graph.cc",
                                     "ninja/src/graphviz.cc",
                                     "ninja/src/jobserver.cc",
                                     "ninja/src/jobserver-posix.cc",
                                     "ninja/src/json.cc",
                                     "ninja/src/lexer.cc",
                                     "ninja/src/line_printer.cc",
                                     "ninja/src/manifest_parser.cc",
                                     "ninja/src/metrics.cc",
                                     "ninja/src/missing_deps.cc",
                                     "ninja/src/parser.cc",
                                     "ninja/src/real_command_runner.cc",
                                     "ninja/src/state.cc",
                                     "ninja/src/status_printer.cc",
                                     "ninja/src/string_piece_util.cc",
                                     "ninja/src/subprocess-posix.cc",
                                     "ninja/src/util.cc",
                                     "ninja/src/version.cc",
                                 });
  ninjacore.add_include_dirs({ "ninja/src" }, ccbuild::Visibility::Private);
  ninjacore.add_compile_options({ "-w" });

  // -- ccbuildlib: ccbuild core (static library) -------------------------
  auto& ccbuildlib =
      p.add_library("ccbuildlib", {
                                      "src/core/target.cc",
                                      "src/targets/executable.cc",
                                      "src/targets/static_library.cc",
                                      "src/internal/ninja_bridge.cc",
                                      "src/internal/compiler.cc",
                                      "src/project/project.cc",
                                  });
  ccbuildlib.add_include_dirs({ "include", "ninja/src", "src" },
                              ccbuild::Visibility::Private);
  ccbuildlib.link(ninjacore);

  // -- ccbuild: CLI executable -------------------------------------------
  auto& cli = p.add_executable("ccbuild", { "src/main.cc" });
  cli.link(ccbuildlib).link(ninjacore);
  cli.add_link_options({ "-lpthread" });

  return p.build();
}
