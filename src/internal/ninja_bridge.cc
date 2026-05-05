#include "internal/ninja_bridge.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "build.h"
#include "build_log.h"
#include "ccbuild/project.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "eval_env.h"
#include "internal/compiler.h"
#include "metrics.h"
#include "state.h"
#include "status_printer.h"

namespace ccbuild {
namespace {

// -- BuildLogUser for fresh builds ---------------------------------------

/// Minimal BuildLogUser: no paths are dead since we always build the
/// full dependency graph from scratch.
struct NullBuildLogUser : public BuildLogUser {
  bool IsPathDead(StringPiece) const override { return false; }
};

// -- EvalString helpers --------------------------------------------------

/// Append literal text to an EvalString.
void text(EvalString& es, const char* s) {
  es.AddText(s);
}

/// Append a variable reference ($var) to an EvalString.
void var(EvalString& es, const char* v) {
  es.AddSpecial(v);
}

// -- Rule Creation -------------------------------------------------------

/// Create the three Ninja build rules (cc, link, ar) and register them
/// with the Ninja State object.
///
/// @param state    The Ninja State into which rules are added.
/// @param compiler Full path to the C++ compiler binary.
/// @param standard The C++ standard version (e.g. 17, 20).
void create_rules(State& state, const std::string& compiler, int standard) {
  const std::string std_flag = "-std=c++" + std::to_string(standard);

  // -- cc: compile a single source file → object file --------------------
  {
    auto rule = std::make_unique<Rule>("cc");

    EvalString cmd;
    text(cmd, (compiler + " " + std_flag + " ").c_str());
    var(cmd, "cflags");  // per-target compile options
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

  // -- link: combine object files → executable ---------------------------
  {
    auto rule = std::make_unique<Rule>("link");

    EvalString cmd;
    text(cmd, (compiler + " ").c_str());
    var(cmd, "ldflags");  // per-target link flags
    text(cmd, " ");
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

  // -- ar: archive object files → static library -------------------------
  {
    auto rule = std::make_unique<Rule>("ar");

    EvalString cmd;
    text(cmd, "ar rcs ");
    var(cmd, "ldflags");  // per-target linker flags
    text(cmd, " ");
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

// -- Target Graph -> Ninja Edges -----------------------------------------

/// Translate a single Target into Ninja Edges and Nodes.
///
/// Steps:
///   1. Build the cflags string from include dirs (with visibility
///      propagation) and compile options.
///   2. Create a compile Edge for each source → object file.
///   3. Create a link or archive Edge to produce the final artifact.
///
/// @return The output Node representing the final build artifact.
Node* add_target_edges(State& state, const Target& target) {
  const Rule* cc_rule = state.bindings_.LookupRule("cc");
  const Rule* link_rule = state.bindings_.LookupRule("link");
  const Rule* ar_rule = state.bindings_.LookupRule("ar");

  // -- Build the per-target cflags string --------------------------------
  std::string cflags;

  {
    std::unordered_set<std::string_view> seen;

    // Append "-I <dir>" for each directory, deduplicated.
    auto add_dir = [&](std::string_view dir) {
      if (seen.insert(dir).second) {
        if (!cflags.empty()) {
          cflags += ' ';
        }
        cflags += "-I";
        cflags += dir;
      }
    };

    auto add_dirs = [&](std::span<const std::string> dirs) {
      for (const auto& d : dirs) {
        add_dir(d);
      }
    };

    // Own directories: PRIVATE + PUBLIC.
    add_dirs(target.include_dirs(Visibility::Private));
    add_dirs(target.include_dirs(Visibility::Public));

    // Transitive directories from link dependencies: PUBLIC + INTERFACE.
    // Uses a depth-first traversal with a visited set to avoid cycles
    // (the project validation already guarantees DAG, but this is defensive).
    std::unordered_set<std::string_view> visited;
    std::function<void(const Target&)> collect = [&](const Target& t) {
      if (!visited.insert(t.name()).second) {
        return;
      }
      add_dirs(t.include_dirs(Visibility::Public));
      add_dirs(t.include_dirs(Visibility::Interface));
      for (const Target& dep : t.link_deps()) {
        collect(dep);
      }
    };

    for (const Target& dep : target.link_deps()) {
      collect(dep);
    }
  }

  // Append per-target compile options to cflags.
  for (const auto& opt : target.compile_options()) {
    if (!cflags.empty()) {
      cflags += ' ';
    }
    cflags += opt;
  }

  // -- Compile each source → object file ---------------------------------
  std::vector<Node*> obj_nodes;
  for (const auto& src : target.sources()) {
    const std::string obj = target.object_path(src);

    // Ensure the output directory exists.
    auto obj_dir = std::filesystem::path(obj).parent_path();
    if (!obj_dir.empty()) {
      std::filesystem::create_directories(obj_dir);
    }

    Edge* edge = state.AddEdge(cc_rule);

    // Set per-edge cflags binding.
    // Note: env_ takes raw ownership via BindingEnv::release().
    // This matches Ninja's internal ownership model.
    auto env = std::make_unique<BindingEnv>(&state.bindings_);
    env->AddBinding("cflags", cflags);
    edge->env_ = env.release();

    state.AddIn(edge, src, 0);
    std::string err;
    state.AddOut(edge, obj, 0, &err);

    obj_nodes.push_back(state.GetNode(obj, 0));
  }

  // -- Final artifact: link or archive -----------------------------------
  const std::string output = target.output_filename();

  // Ensure the output directory exists (e.g. .ccbuild/bin/, .ccbuild/lib/).
  auto out_dir = std::filesystem::path(output).parent_path();
  if (!out_dir.empty()) {
    std::filesystem::create_directories(out_dir);
  }

  const Rule* final_rule =
      (target.kind() == TargetKind::Executable) ? link_rule : ar_rule;

  Edge* final_edge = state.AddEdge(final_rule);
  auto final_env = std::make_unique<BindingEnv>(&state.bindings_);

  // Build ldflags from per-target link options.
  std::string ldflags;
  for (const auto& opt : target.link_options()) {
    if (!ldflags.empty()) {
      ldflags += ' ';
    }
    ldflags += opt;
  }
  final_env->AddBinding("ldflags", ldflags);
  final_edge->env_ = final_env.release();

  // Inputs: all compiled object files.
  for (auto* obj_node : obj_nodes) {
    state.AddIn(final_edge, obj_node->path(), 0);
  }

  // Libraries: only archive own object files (no link-dep archives).
  // Executables: recursively collect all transitive library archives.
  if (target.kind() == TargetKind::Executable) {
    std::unordered_set<std::string_view> link_visited;
    std::function<void(const Target&)> collect_link_inputs =
        [&](const Target& t) {
          for (const Target& dep : t.link_deps()) {
            if (link_visited.insert(dep.name()).second) {
              const std::string dep_output = dep.output_filename();
              state.AddIn(final_edge, dep_output, 0);
              collect_link_inputs(dep);
            }
          }
        };
    collect_link_inputs(target);
  }

  std::string err;
  state.AddOut(final_edge, output, 0, &err);

  return state.GetNode(output, 0);
}

}  // namespace

// -- Public Interface ----------------------------------------------------

int NinjaBridge::build(const Project& project) {
  // 1. Detect the C++ compiler.
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

  // 2. Create the Ninja build state and rules.
  State state;
  create_rules(state, compiler->path, project.standard());

  // 3. Add edges for each target.
  //    Libraries are processed before executables so their output nodes
  //    exist when executables reference them as link inputs.
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

  // 4. Configure the builder.
  BuildConfig config;
  config.verbosity = BuildConfig::NORMAL;
  config.parallelism = std::thread::hardware_concurrency();

  static constexpr int kDefaultParallelism = 4;
  if (config.parallelism == 0) {
    config.parallelism = kDefaultParallelism;
  }

  // 5. Set up build infrastructure (logs, disk, status).
  BuildLog build_log;
  DepsLog deps_log;
  RealDiskInterface disk_interface;
  StatusPrinter status(config);

  std::filesystem::create_directories(".ccbuild");

  std::string err;
  NullBuildLogUser log_user;

  // Load existing logs for incremental build support.
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

  // 6. Create the builder and add all output nodes as build targets.
  const int64_t start = GetTimeMillis();
  Builder builder(&state, config, &build_log, &deps_log, &disk_interface,
                  &status, start);

  for (auto* node : output_nodes) {
    if (!builder.AddTarget(node, &err)) {
      fprintf(stderr, "ccbuild: error adding target: %s\n", err.c_str());
      return 1;
    }
  }

  // 7. Check if already up to date.
  if (builder.AlreadyUpToDate()) {
    printf("ccbuild: nothing to do, already up to date.\n");
    return 0;
  }

  // 8. Run the build.
  const ExitStatus exit_status = builder.Build(&err);
  if (exit_status != ExitSuccess) {
    fprintf(stderr, "ccbuild: build failed: %s\n", err.c_str());
    return static_cast<int>(exit_status);
  }

  return 0;
}

}  // namespace ccbuild
