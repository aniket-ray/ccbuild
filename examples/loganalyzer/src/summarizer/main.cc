/// logsummary — Compute statistics over a CSV log file.
///
/// Usage: logsummary <file.csv>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "analyzer/log_entry.h"
#include "csv/parser.h"
#include "stats/counter.h"
#include "stats/summary.h"

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <file.csv>\n", argv[0]);
    return 1;
  }

  // Load and parse.
  csv::Table table;
  try {
    table = csv::parse_file(argv[1]);
  } catch (const std::exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }

  auto entries = analyzer::parse_entries(table);
  printf("Log summary for: %s\n", argv[1]);
  printf("Total entries: %zu\n\n", entries.size());

  // Severity breakdown.
  stats::Counter severity_counts;
  for (const auto& e : entries)
    severity_counts.add(analyzer::severity_to_string(e.severity));

  printf("--- Severity Distribution ---\n");
  for (const auto& [level, count] : severity_counts.sorted()) {
    double pct = 100.0 * static_cast<double>(count) /
                 static_cast<double>(severity_counts.total());
    printf("  %-8s %5zu  (%5.1f%%)\n", level.c_str(), count, pct);
  }
  printf("\n");

  // Top sources.
  stats::Counter source_counts;
  for (const auto& e : entries)
    source_counts.add(e.source);

  printf("--- Top Sources (by frequency) ---\n");
  for (const auto& [src, count] : source_counts.top(10)) {
    printf("  %-20s %5zu\n", src.c_str(), count);
  }
  printf("  (%zu unique sources)\n\n", source_counts.unique());

  // Response time stats (where available).
  stats::Summary response_stats;
  for (const auto& e : entries) {
    if (e.response_ms >= 0)
      response_stats.add(static_cast<double>(e.response_ms));
  }

  if (response_stats.count() > 0) {
    printf("--- Response Time (ms) ---\n");
    printf("  %s\n\n", response_stats.to_string().c_str());
  }

  // Error entries sample.
  printf("--- Recent Errors ---\n");
  int error_count = 0;
  for (const auto& e : entries) {
    if (e.severity >= analyzer::Severity::ERROR) {
      printf("  [%s] %s: %s\n", e.timestamp.c_str(), e.source.c_str(),
             e.message.c_str());
      if (++error_count >= 5) {
        printf("  ... (showing first 5)\n");
        break;
      }
    }
  }
  if (error_count == 0)
    printf("  (none)\n");

  return 0;
}
