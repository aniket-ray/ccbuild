/// loganalyzer — build.cc
///
/// This project exercises ccbuild with:
///   - 3 static libraries (csv, stats, analyzer)
///   - 2 executables (logview, logsummary)
///   - Inter-library dependencies (analyzer → csv)
///   - Compile options (-Isrc for cross-directory includes)
///   - Multiple source files per target
///   - Nested directory structure

#include <ccbuild/ccbuild.h>

int main() {
  ccbuild::Project p("loganalyzer");
  p.set_cxx_standard(17);

  // --- Libraries ---

  // CSV parsing library (standalone, no deps).
  auto& csv =
      p.add_library("csv", { "src/csv/parser.cc", "src/csv/writer.cc" });
  csv.add_compile_options({ "-Isrc" });

  // Statistics library (standalone, no deps).
  auto& stats = p.add_library(
      "stats", { "src/stats/summary.cc", "src/stats/counter.cc" });
  stats.add_compile_options({ "-Isrc" });

  // Log analyzer library — depends on csv for parsing log files.
  auto& analyzer = p.add_library(
      "analyzer", { "src/analyzer/log_entry.cc", "src/analyzer/filter.cc" });
  analyzer.add_compile_options({ "-Isrc" });
  analyzer.link(csv);

  // --- Executables ---

  // logview: view and filter log entries.
  auto& logview = p.add_executable("logview", { "src/viewer/main.cc" });
  logview.add_compile_options({ "-Isrc", "-Wall", "-Wextra" });
  logview.link(analyzer).link(csv);

  // logsummary: compute statistics over log entries.
  auto& logsummary =
      p.add_executable("logsummary", { "src/summarizer/main.cc" });
  logsummary.add_compile_options({ "-Isrc", "-Wall", "-Wextra" });
  logsummary.link(analyzer).link(csv).link(stats);

  return p.build();
}
