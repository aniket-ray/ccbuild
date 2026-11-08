#include "internal/ninja_bridge.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "build.h"
#include "build_log.h"
#include "ccbuild/project.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "eval_env.h"
#include "internal/compiler.h"
#include "internal/layout.h"
#include "internal/link_graph.h"
#include "metrics.h"
#include "state.h"
#include "status_printer.h"

namespace ccbuild::internal {
namespace {

/// Parallelism used when the number of CPUs cannot be determined.
constexpr int kDefaultParallelism = 4;

/// Minimal BuildLogUser: no path is ever considered dead, since the full
/// graph is rebuilt in memory on every run.
struct NullBuildLogUser final : public BuildLogUser {
  bool IsPathDead(StringPiece /*path*/) const override { return false; }
};

/// Append `fragment` to the shell command fragment `out`, space-separated.
void append_fragment(std::string& out, std::string_view fragment) {
  if (!out.empty()) {
    out += ' ';
  }
  out += fragment;
}

/// The compiler flags for `target`'s sources: -I for each include
/// directory, then the compile options, inserted as written.
std::string compile_flags(const Target& target) {
  std::string flags;
  for (const auto& dir : compile_include_dirs(target)) {
    append_fragment(flags, "-I" + dir);
  }
  for (const auto& option : target.compile_options()) {
    append_fragment(flags, option);
  }
  return flags;
}

/// The linker flags for executable `target`, inserted as written.
std::string linker_flags(const Target& target) {
  std::string flags;
  for (const auto& option : link_flags(target)) {
    append_fragment(flags, option);
  }
  return flags;
}

/// Translates a Project into Ninja rules and edges inside a Ninja State.
///
/// Ninja's Edge refers to its binding environment through a raw, non-owning
/// pointer; this class owns those environments, so it must outlive any use
/// of the State's edges (i.e. the Builder).
class GraphBuilder {
 public:
  GraphBuilder(State& state, const CompilerInfo& compiler, int cxx_standard)
      : state_(state) {
    create_rules(compiler.path, cxx_standard);
  }

  /// Add the compile edges and the final link/archive edge for `target`.
  ///
  /// @return The node of the final artifact, or nullptr with `err` set.
  Node* add_target(const Target& target, std::string& err) {
    const std::string cflags = compile_flags(target);

    // One compile edge per source file.
    std::vector<std::string> objects;
    objects.reserve(target.sources().size());
    for (const auto& src : target.sources()) {
      objects.push_back(target.object_path(src));
      const std::string& source = src;
      if (!add_edge(cc_rule_, std::span(&source, 1), objects.back(),
                    { { "cflags", cflags } }, err)) {
        return nullptr;
      }
    }

    // The final artifact.
    const std::string output = target.output_filename();
    if (target.kind() == TargetKind::Executable) {
      // Link inputs: own objects, then every transitive library archive in
      // dependency order; library link options are inherited.
      std::vector<std::string> inputs = std::move(objects);
      for (const Target* lib : link_order(target)) {
        inputs.push_back(lib->output_filename());
      }
      if (!add_edge(link_rule_, inputs, output,
                    { { "ldflags", linker_flags(target) } }, err)) {
        return nullptr;
      }
    } else {
      // A static library archives only its own objects.
      if (!add_edge(ar_rule_, objects, output, {}, err)) {
        return nullptr;
      }
    }
    return state_.LookupNode(output);
  }

 private:
  using Bindings = std::vector<std::pair<std::string, std::string>>;

  /// Register the three rules with the State.
  void create_rules(const std::string& compiler, int cxx_standard) {
    const std::string std_flag = "-std=c++" + std::to_string(cxx_standard);

    // cc: compile one source file into an object file, recording header
    // dependencies in a depfile that Ninja moves into its deps log.
    {
      auto rule = std::make_unique<Rule>("cc");
      EvalString command;
      command.AddText(compiler + " " + std_flag + " ");
      command.AddSpecial("cflags");
      command.AddText(" -MD -MF ");
      command.AddSpecial("out");
      command.AddText(".d -c ");
      command.AddSpecial("in");
      command.AddText(" -o ");
      command.AddSpecial("out");
      rule->AddBinding("command", command);
      rule->AddBinding("depfile", eval({ out(), text(".d") }));
      rule->AddBinding("deps", eval({ text("gcc") }));
      rule->AddBinding("description", eval({ text("CC "), out() }));
      cc_rule_ = add_rule(std::move(rule));
    }

    // link: link objects and archives into an executable.  Link flags come
    // last so that -l libraries resolve symbols from everything before them.
    {
      auto rule = std::make_unique<Rule>("link");
      EvalString command;
      command.AddText(compiler + " -o ");
      command.AddSpecial("out");
      command.AddText(" ");
      command.AddSpecial("in");
      command.AddText(" ");
      command.AddSpecial("ldflags");
      rule->AddBinding("command", command);
      rule->AddBinding("description", eval({ text("LINK "), out() }));
      link_rule_ = add_rule(std::move(rule));
    }

    // ar: archive objects into a static library.
    {
      auto rule = std::make_unique<Rule>("ar");
      EvalString command;
      command.AddText("ar rcs ");
      command.AddSpecial("out");
      command.AddText(" ");
      command.AddSpecial("in");
      rule->AddBinding("command", command);
      rule->AddBinding("description", eval({ text("AR "), out() }));
      ar_rule_ = add_rule(std::move(rule));
    }
  }

  /// One piece of an EvalString: literal text, or a $variable reference.
  struct Piece {
    std::string value;
    bool is_variable = false;
  };
  static Piece text(std::string s) { return { std::move(s), false }; }
  static Piece out() { return { "out", true }; }

  static EvalString eval(std::initializer_list<Piece> pieces) {
    EvalString result;
    for (const auto& piece : pieces) {
      if (piece.is_variable) {
        result.AddSpecial(piece.value);
      } else {
        result.AddText(piece.value);
      }
    }
    return result;
  }

  const Rule* add_rule(std::unique_ptr<Rule> rule) {
    const Rule* ptr = rule.get();
    state_.bindings_.AddRule(std::move(rule));
    return ptr;
  }

  /// Add an edge `inputs -> output` using `rule`, with per-edge bindings.
  bool add_edge(const Rule* rule, std::span<const std::string> inputs,
                const std::string& output, const Bindings& bindings,
                std::string& err) {
    Edge* edge = state_.AddEdge(rule);

    auto env = std::make_unique<BindingEnv>(&state_.bindings_);
    for (const auto& [key, value] : bindings) {
      env->AddBinding(key, value);
    }
    edge->env_ = env.get();
    envs_.push_back(std::move(env));

    for (const auto& input : inputs) {
      state_.AddIn(edge, input, 0);
    }
    return state_.AddOut(edge, output, 0, &err);
  }

  State& state_;
  const Rule* cc_rule_ = nullptr;
  const Rule* link_rule_ = nullptr;
  const Rule* ar_rule_ = nullptr;
  std::vector<std::unique_ptr<BindingEnv>> envs_;
};

/// Number of parallel jobs: one per CPU.
int default_parallelism() {
  const unsigned cpus = std::thread::hardware_concurrency();
  return (cpus == 0) ? kDefaultParallelism : static_cast<int>(cpus);
}

}  // namespace

// -- Public Interface ----------------------------------------------------

int NinjaBridge::build(const Project& project) {
  // 1. Detect the C++ compiler.
  const auto compiler = detect_compiler();
  if (!compiler) {
    std::fprintf(stderr,
                 "ccbuild: error: no C++ compiler found.\n"
                 "  Set $CXX or install g++/clang++.\n");
    return 1;
  }
  std::fprintf(stderr, "ccbuild: using %.*s %s (%s)\n",
               static_cast<int>(compiler->kind_str().size()),
               compiler->kind_str().data(), compiler->version.c_str(),
               compiler->path.c_str());

  // 2. Build the Ninja graph.  Libraries first, so their archive nodes
  //    exist (with their producing edges) before executables reference them.
  State state;
  GraphBuilder graph(state, *compiler, project.standard());
  std::vector<Node*> outputs;
  std::string err;
  for (const TargetKind kind :
       { TargetKind::StaticLibrary, TargetKind::Executable }) {
    for (const auto& target : project.targets()) {
      if (target->kind() != kind) {
        continue;
      }
      Node* node = graph.add_target(*target, err);
      if (node == nullptr) {
        std::fprintf(stderr, "ccbuild: error: target '%.*s': %s\n",
                     static_cast<int>(target->name().size()),
                     target->name().data(), err.c_str());
        return 1;
      }
      outputs.push_back(node);
    }
  }

  // 3. Open the build and deps logs (they make builds incremental).
  std::error_code ec;
  std::filesystem::create_directories(layout::kBuildDir, ec);
  if (ec) {
    std::fprintf(stderr, "ccbuild: error: cannot create %s: %s\n",
                 std::string(layout::kBuildDir).c_str(), ec.message().c_str());
    return 1;
  }

  const std::string build_log_path(layout::kBuildLogPath);
  const std::string deps_log_path(layout::kDepsLogPath);
  BuildLog build_log;
  DepsLog deps_log;
  const NullBuildLogUser log_user;

  if (build_log.Load(build_log_path, &err) == LOAD_ERROR) {
    std::fprintf(stderr, "ccbuild: warning: loading build log: %s\n",
                 err.c_str());
    err.clear();
  }
  if (!build_log.OpenForWrite(build_log_path, log_user, &err)) {
    std::fprintf(stderr, "ccbuild: error: opening build log: %s\n",
                 err.c_str());
    return 1;
  }
  if (deps_log.Load(deps_log_path, &state, &err) == LOAD_ERROR) {
    std::fprintf(stderr, "ccbuild: warning: loading deps log: %s\n",
                 err.c_str());
    err.clear();
  }
  if (!deps_log.OpenForWrite(deps_log_path, &err)) {
    std::fprintf(stderr, "ccbuild: error: opening deps log: %s\n", err.c_str());
    return 1;
  }

  // 4. Run the build.
  BuildConfig config;
  config.verbosity = BuildConfig::NORMAL;
  config.parallelism = default_parallelism();

  RealDiskInterface disk_interface;
  StatusPrinter status(config);
  Builder builder(&state, config, &build_log, &deps_log, &disk_interface,
                  &status, GetTimeMillis());

  for (Node* node : outputs) {
    if (!builder.AddTarget(node, &err)) {
      std::fprintf(stderr, "ccbuild: error: %s\n", err.c_str());
      return 1;
    }
  }

  if (builder.AlreadyUpToDate()) {
    std::printf("ccbuild: nothing to do, already up to date.\n");
    return 0;
  }

  const ExitStatus exit_status = builder.Build(&err);
  if (exit_status != ExitSuccess) {
    std::fprintf(stderr, "ccbuild: build failed: %s\n", err.c_str());
    return static_cast<int>(exit_status);
  }
  return 0;
}

}  // namespace ccbuild::internal
