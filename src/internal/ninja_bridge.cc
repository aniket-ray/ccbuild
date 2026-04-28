#include "internal/ninja_bridge.h"

#include "ccbuild/project.h"
#include "internal/compiler.h"

// Ninja headers.
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "build.h"
#include "build_log.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "eval_env.h"
#include "metrics.h"
#include "state.h"
#include "status_printer.h"

namespace ccbuild {
namespace {

/// Minimal BuildLogUser — no paths are dead since we build the full graph.
struct NullBuildLogUser : public BuildLogUser {
  bool IsPathDead(StringPiece) const override { return false; }
};

/// Helper: build an EvalString from text and $variable references.
/// Append literal text to an EvalString.
void text(EvalString& es, const char* s) {
  es.AddText(s);
}

/// Append a variable reference ($var) to an EvalString.
void var(EvalString& es, const char* v) {
  es.AddSpecial(v);
}

/// Create the three build rules: cc, link, ar.
void create_rules(State& state, const std::string& compiler, int standard) {
  std::string std_flag = "-std=c++" + std::to_string(standard);

  {
    auto rule = std::make_unique<Rule>("cc");

    EvalString cmd;
    text(cmd, (compiler + " " + std_flag + " ").c_str());
    // Per-target compile options injected via $cflags.
    var(cmd, "cflags");
    text(cmd, " -MD -MF ");
    var(cmd, "out");
    text(cmd, ".d -c ");
    var(cmd, "in");
    text(cmd, " -o ");
    var(cmd, "out");
    rule->AddBinding("command", cmd);

    EvalString depfile;
    var(depfile, "out");
    text(depfile, ".d");
    rule->AddBinding("depfile", depfile);

    EvalString deps;
    text(deps, "gcc");
    rule->AddBinding("deps", deps);

    EvalString desc;
    text(desc, "CC ");
    var(desc, "out");
    rule->AddBinding("description", desc);

    state.bindings_.AddRule(std::move(rule));
  }

  // -- link: link object files -> executable ----------------------------
  {
    auto rule = std::make_unique<Rule>("link");

    EvalString cmd;
    text(cmd, (compiler + " ").c_str());
    var(cmd, "in");
    text(cmd, " -o ");
    var(cmd, "out");
    rule->AddBinding("command", cmd);

    EvalString desc;
    text(desc, "LINK ");
    var(desc, "out");
    rule->AddBinding("description", desc);

    state.bindings_.AddRule(std::move(rule));
  }

  // -- ar: archive object files -> static library -----------------------
  {
    auto rule = std::make_unique<Rule>("ar");

    EvalString cmd;
    text(cmd, "ar rcs ");
    var(cmd, "out");
    text(cmd, " ");
    var(cmd, "in");
    rule->AddBinding("command", cmd);

    EvalString desc;
    text(desc, "AR ");
    var(desc, "out");
    rule->AddBinding("description", desc);

    state.bindings_.AddRule(std::move(rule));
  }
}

// Add edges for a single target to the State.
// Returns the output Node* for the final artifact (executable or .a).
Node* add_target_edges(State& state, const Target& target) {
  const Rule* cc_rule = state.bindings_.LookupRule("cc");
  const Rule* link_rule = state.bindings_.LookupRule("link");
  const Rule* ar_rule = state.bindings_.LookupRule("ar");

  // Build per-target cflags string.
  std::string cflags;
  for (const auto& opt : target.compile_options()) {
    if (!cflags.empty())
      cflags += ' ';
    cflags += opt;
  }

  // Compile each source -> object file.
  std::vector<Node*> obj_nodes;
  for (const auto& src : target.sources()) {
    std::string obj = target.object_path(src);

    // Ensure the output directory exists.
    auto obj_dir = std::filesystem::path(obj).parent_path();
    if (!obj_dir.empty())
      std::filesystem::create_directories(obj_dir);

    Edge* edge = state.AddEdge(cc_rule);

    // Set per-edge cflags binding.
    auto* env = new BindingEnv(&state.bindings_);
    env->AddBinding("cflags", cflags);
    edge->env_ = env;

    state.AddIn(edge, src, 0);
    std::string err;
    state.AddOut(edge, obj, 0, &err);

    obj_nodes.push_back(state.GetNode(obj, 0));
  }

  // link (executable) or archive (static library).
  std::string output = target.output_filename();

  // Ensure the output directory exists (e.g. .ccbuild/bin/, .ccbuild/lib/).
  auto out_dir = std::filesystem::path(output).parent_path();
  if (!out_dir.empty())
    std::filesystem::create_directories(out_dir);

  const Rule* final_rule =
      (target.kind() == TargetKind::Executable) ? link_rule : ar_rule;

  Edge* final_edge = state.AddEdge(final_rule);
  final_edge->env_ = new BindingEnv(&state.bindings_);

  // Add all object files as inputs.
  for (auto* obj_node : obj_nodes) {
    state.AddIn(final_edge, obj_node->path(), 0);
  }

  // Add link dependency outputs as inputs (for executables linking libraries).
  for (const auto* dep : target.link_deps()) {
    std::string dep_output = dep->output_filename();
    state.AddIn(final_edge, dep_output, 0);
  }

  std::string err;
  state.AddOut(final_edge, output, 0, &err);

  return state.GetNode(output, 0);
}

}  // namespace

int NinjaBridge::build(const Project& project) {
  auto compiler = detect_compiler();
  if (!compiler) {
    fprintf(stderr,
            "ccbuild: error: no C++ compiler found.\n"
            "  Set $CXX or install g++/clang++.\n");
    return 1;
  }
  fprintf(stderr, "ccbuild: using %.*s %s (%s)\n",
          static_cast<int>(compiler->kind_str().size()),
          compiler->kind_str().data(), compiler->version.c_str(),
          compiler->path.c_str());

  State state;
  create_rules(state, compiler->path, project.standard());

  // Add edges for each target
  //    Process libraries before executables so their outputs exist
  //    when executables reference them as link inputs
  std::vector<Node*> output_nodes;
  for (const auto& t : project.targets()) {
    if (t->kind() == TargetKind::StaticLibrary) {
      output_nodes.push_back(add_target_edges(state, *t));
    }
  }
  for (const auto& t : project.targets()) {
    if (t->kind() == TargetKind::Executable) {
      output_nodes.push_back(add_target_edges(state, *t));
    }
  }

  // Configure the build
  BuildConfig config;
  config.verbosity = BuildConfig::NORMAL;
  config.parallelism = std::thread::hardware_concurrency();
  if (config.parallelism == 0)
    config.parallelism = 4;

  // Set up build infrastructure
  BuildLog build_log;
  DepsLog deps_log;
  RealDiskInterface disk_interface;
  StatusPrinter status(config);

  std::filesystem::create_directories(".ccbuild");

  // Load existing logs for incremental build support, then open for writing
  std::string err;
  NullBuildLogUser log_user;
  if (!build_log.Load(".ccbuild/.ninja_log", &err)) {
    fprintf(stderr, "ccbuild: warning: loading build log: %s\n", err.c_str());
    err.clear();
  }
  if (!build_log.OpenForWrite(".ccbuild/.ninja_log", log_user, &err)) {
    fprintf(stderr, "ccbuild: error opening build log: %s\n", err.c_str());
    return 1;
  }
  if (!deps_log.Load(".ccbuild/.ninja_deps", &state, &err)) {
    fprintf(stderr, "ccbuild: warning: loading deps log: %s\n", err.c_str());
    err.clear();
  }
  if (!deps_log.OpenForWrite(".ccbuild/.ninja_deps", &err)) {
    fprintf(stderr, "ccbuild: error opening deps log: %s\n", err.c_str());
    return 1;
  }

  int64_t start = GetTimeMillis();
  Builder builder(&state, config, &build_log, &deps_log, &disk_interface,
                  &status, start);

  // Add all output nodes as build targets
  for (auto* node : output_nodes) {
    if (!builder.AddTarget(node, &err)) {
      fprintf(stderr, "ccbuild: error adding target: %s\n", err.c_str());
      return 1;
    }
  }

  // Check if already up to date
  if (builder.AlreadyUpToDate()) {
    printf("ccbuild: nothing to do, already up to date.\n");
    return 0;
  }

  // Run the build
  ExitStatus exit_status = builder.Build(&err);
  if (exit_status != ExitSuccess) {
    fprintf(stderr, "ccbuild: build failed: %s\n", err.c_str());
    return static_cast<int>(exit_status);
  }

  return 0;
}

}  // namespace ccbuild
