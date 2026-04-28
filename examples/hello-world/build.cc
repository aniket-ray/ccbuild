#include <ccbuild/ccbuild.h>

int main() {
  ccbuild::Project p("hello");
  p.set_cxx_standard(17);

  auto& lib = p.add_library("greet", { "src/greet.cc" });
  auto& exe = p.add_executable("hello", { "src/main.cc" });
  exe.link(lib);

  return p.build();
}
