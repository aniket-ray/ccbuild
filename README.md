<div align="center">
  <h1>ccbuild</h1>
  <p><strong>A build system for C++ where your build scripts are C++.</strong></p>

  [![CI](https://github.com/aniket-ray/ccbuild/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/aniket-ray/ccbuild/actions/workflows/ci.yml)
  [![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
  [![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)
  [![codecov](https://codecov.io/gh/aniket-ray/ccbuild/branch/main/graph/badge.svg)](https://codecov.io/gh/aniket-ray/ccbuild)
</div>

---

Instead of learning a new domain-specific language or configuration format like CMake or Make, **ccbuild** lets you describe your project in a plain `build.cc` file using a clean C++ API.

The `ccbuild` CLI compiles your build script, caches the result, and executes it—driving fast, parallel, incremental compilation via [Ninja](https://ninja-build.org/) under the hood.

## Features

- **No New Language:** Write your build scripts in C++. Benefit from your IDE's autocompletion, type safety, and syntax highlighting.
- **Blazing Fast:** Powered by Ninja. Automatically uses all available CPU cores and only recompiles what changed, including when headers change.
- **Smart Validation:** Catch invalid or duplicate target names, targets without sources, sources that would overwrite each other's object files, unsupported C++ standards, and link cycles *before* compilation begins.
- **Correct by Default:** Static libraries are linked in dependency order, source and include paths with spaces just work, and concurrent `ccbuild` runs in one project take turns instead of clobbering each other.
- **Zero Clutter:** All build artifacts (binaries, libraries, and objects) are neatly isolated in a `.ccbuild/` directory.

## Quick Start

### 1. Bootstrap via CMake

Ensure you have a C++20 compiler (CI tests GCC 13 and Clang 18), CMake 3.16+, and a POSIX system (macOS/Linux).

```bash
git clone --recursive https://github.com/aniket-ray/ccbuild.git
cd ccbuild   # (already cloned? run: git submodule update --init)

# Bootstrap: build with CMake, then install to /usr/local (or your prefix)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
sudo cmake --install build
```

The install step places the binary (`ccbuild`), libraries (`libccbuildlib.a`, `libninjacore.a`), and headers (`ccbuild/*.h`) under the install prefix. The install path is baked into the binary at compile time — no directory crawling needed at runtime.

> **Self-hosting:** After the initial CMake bootstrap, ccbuild can rebuild itself:
> ```bash
> cd ccbuild
> ccbuild            # uses build.cc to rebuild itself
> ```

### 2. Write your `build.cc`

Place a `build.cc` file at the root of your project:

```cpp
#include <ccbuild/ccbuild.h>

int main() {
  ccbuild::Project p("myapp");
  p.set_cxx_standard(20);

  auto& lib = p.add_library("utils", { "src/utils.cc" });
  auto& exe = p.add_executable("myapp", { "src/main.cc" });

  exe.link(lib);
  exe.add_compile_options({ "-Wall", "-Wextra", "-O3" });

  return p.build();
}
```

### 3. Build & Run

```console
$ ccbuild
ccbuild: compiling build.cc...
ccbuild: project 'myapp' (C++20)
  target 'utils' [static library] -> .ccbuild/lib/libutils.a
    sources: src/utils.cc
    compile: src/utils.cc -> .ccbuild/obj/utils/src/utils.o
    archive: -> .ccbuild/lib/libutils.a
  target 'myapp' [executable] -> .ccbuild/bin/myapp
    sources: src/main.cc
    options: -Wall -Wextra -O3
    links: utils
    compile: src/main.cc -> .ccbuild/obj/myapp/src/main.o
    link: -> .ccbuild/bin/myapp
  plan: 2 compile, 1 archive, 1 link
ccbuild: using GCC 13.2.0 (/usr/bin/g++)
[1/4] CC .ccbuild/obj/myapp/src/main.o
[2/4] CC .ccbuild/obj/utils/src/utils.o
[3/4] AR .ccbuild/lib/libutils.a
[4/4] LINK .ccbuild/bin/myapp

$ .ccbuild/bin/myapp
```

Run `ccbuild --help` for the command-line options and `ccbuild --version` to print the version.

## API Overview

The `ccbuild` API is designed to be fluent and intuitive.

- **`ccbuild::Project`**: The top-level container for your build.
  - `add_executable(name, sources)` / `add_library(name, sources)` — `sources` is a braced list or any range of strings (e.g. a `std::vector<std::string>` built at runtime).
  - `set_cxx_standard(n)` — one of 98, 11, 14, 17, 20, 23, 26 (default 17); your compiler must support it.
  - `build(dry_run = false)`: Validates, prints the build plan, and (unless `dry_run`) runs the build. Returns a process exit code and never throws.

- **`ccbuild::Target`**: The base class for executables and static libraries.
  - `add_sources(sources)`
  - `add_compile_options(options)` — compiler flags for this target's sources.
  - `add_include_dirs(dirs, Visibility::Private | Public | Interface)` — `Public` and `Interface` directories propagate to every target that links this one.
  - `add_link_options(options)` — linker flags, placed after all objects and libraries. On a library, they apply to every executable that links it (e.g. `-lpthread`).
  - `link(target)`: Chainable dependency linking.

Every list-taking method accepts either a braced list or any range of strings. Sources and include directories are paths: each is passed as one argument, whatever characters it contains. Compile and link options are shell syntax, inserted into the command line as written, so `"-O2 -g"` and `"$(pkg-config --cflags zlib)"` work.

*For complete examples (like inter-library dependencies and multi-target projects), see the [`examples/`](examples) directory.*

## Architecture

1. **Compile**: When you run `ccbuild`, it compiles your `build.cc` into a cached runner binary (`.ccbuild/runner`). The runner is recompiled whenever `build.cc`, the installed ccbuild libraries, or the compile command change (tracked by a fingerprint in `.ccbuild/runner.stamp`), and a failed compile never replaces a working runner. A lock on `.ccbuild/` makes concurrent invocations wait their turn.
2. **Execute**: The runner executes your `main()` function, constructing the project graph in memory.
3. **Validate**: The graph is validated for correctness (e.g. unique names, no object-file collisions, no cycles).
4. **Build**: The Ninja bridge translates your targets into build edges and drives Ninja's incremental build engine.

ccbuild runs its own subprocesses (the compiler for `build.cc`, the runner) with `posix_spawn` and argument vectors, never through a shell. The per-file compile and link commands are run by Ninja through `/bin/sh`, with every path quoted.

### Source layout

```
include/ccbuild/    Public API: Project, Target, Executable, StaticLibrary, version
src/core/           Implementation of the public API
src/internal/       Engine internals (not installed):
  validators          pre-build checks on the project model
  link_graph          link order, include-dir and link-flag propagation
  ninja_bridge        Project -> Ninja rules/edges; runs Ninja's Builder
  compiler            C++ compiler detection ($CXX, g++, clang++)
  process             shell-free subprocess execution and PATH lookup
  plan_printer        the human-readable build plan
  layout.h            every path under .ccbuild/, in one place
src/cli/            The ccbuild command: argument parsing, runner caching
tests/              GoogleTest suites, mirroring src/
build.cc            ccbuild's own build script (self-hosting)
```

## Contributing

Contributions are welcome! To build and run the test suite locally:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCCBUILD_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure -j
```

Tests run in isolated temporary directories, so they can run in parallel.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `CCBUILD_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings in ccbuild's own code as errors (CI turns this on). |
| `CCBUILD_SANITIZERS` | empty | Build with sanitizers, e.g. `address,undefined`. Run tests with `LSAN_OPTIONS=suppressions=$PWD/tests/lsan.supp`. |
| `CCBUILD_CLANG_TIDY` | `OFF` | Run clang-tidy (see `.clang-tidy`) while compiling ccbuild's sources. |
| `CCBUILD_COVERAGE` | `OFF` | Instrument for coverage; adds a `coverage` target (requires `llvm-cov` or `lcov`). |

To generate a code coverage report:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCCBUILD_COVERAGE=ON
cmake --build build --target coverage
```

Code is formatted with clang-format (CI pins version 23.1.3: `pipx install clang-format==23.1.3`):
```bash
find src include examples tests -name '*.cc' -o -name '*.h' | xargs clang-format -i
clang-format -i build.cc
```

When adding a source file, add it to both `CMakeLists.txt` and the self-hosting `build.cc`; CI builds ccbuild with itself and then builds an example against the self-built libraries.

## License

This project is licensed under the [MIT License](LICENSE).
