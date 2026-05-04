<div align="center">
  <h1>ccbuild</h1>
  <p><strong>A build system for C++ where your build scripts are C++.</strong></p>

  [![CI](https://github.com/aniket-ray/ccbuild/actions/workflows/ci.yml/badge.svg?branch=development)](https://github.com/aniket-ray/ccbuild/actions/workflows/ci.yml)
  [![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
  [![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)
</div>

---

Instead of learning a new domain-specific language or configuration format like CMake or Make, **ccbuild** lets you describe your project in a plain `build.cc` file using a clean C++ API. 

The `ccbuild` CLI compiles your build script, caches the result, and executes it—driving fast, parallel, incremental compilation via [Ninja](https://ninja-build.org/) under the hood.

## Features

- **No New Language:** Write your build scripts in C++. Benefit from your IDE's autocompletion, type safety, and syntax highlighting.
- **Blazing Fast:** Powered by Ninja. Automatically uses all available CPU cores and only recompiles what changed.
- **Smart Validation:** Catch duplicate target names, missing sources, and dependency cycles *before* compilation begins.
- **Zero Clutter:** All build artifacts (binaries, libraries, and objects) are neatly isolated in a `.ccbuild/` directory.

## Quick Start

### 1. Bootstrap via CMake

Ensure you have a C++20 compiler, CMake 3.15+, and a POSIX system (macOS/Linux).

```bash
git clone --recursive https://github.com/aniket-ray/ccbuild.git
cd ccbuild

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

```bash
$ ccbuild
ccbuild: compiling build.cc...
ccbuild: using Clang 17.0.0 (/usr/bin/clang++)
ccbuild: project 'myapp' (C++20)
[1/3] CC .ccbuild/obj/utils/src/utils.o
[2/3] AR .ccbuild/lib/libutils.a
[3/3] LINK .ccbuild/bin/myapp

$ .ccbuild/bin/myapp
```

## API Overview

The `ccbuild` API is designed to be fluent and intuitive.

- **`ccbuild::Project`**: The top-level container for your build.
  - `add_executable(name, sources)`
  - `add_library(name, sources)`
  - `build(dry_run = false)`: Validates, generates the Ninja graph, and executes the build.

- **`ccbuild::Target`**: The base class for executables and static libraries.
  - `add_sources(sources)`
  - `add_compile_options(options)`
  - `link(target)`: Chainable dependency linking.

*For complete examples (like inter-library dependencies and multi-target projects), see the [`examples/`](examples) directory.*

## Architecture 

1. **Compile**: When you run `ccbuild`, it compiles your `build.cc` into a cached runner binary (`.ccbuild/runner`).
2. **Execute**: The runner executes your `main()` function, constructing the project graph in memory.
3. **Validate**: The graph is validated for correctness (e.g., preventing cycles).
4. **Build**: The internal Ninja bridge translates your targets into build edges and drives the incremental build process.

## Contributing

Contributions are welcome! To run the test suite locally:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCCBUILD_COVERAGE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

To generate a code coverage report (requires `llvm-cov` or `lcov`):
```bash
cmake --build build --target coverage
```

## License

This project is licensed under the [MIT License](LICENSE).
