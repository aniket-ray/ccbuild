/// logview — View and filter log entries from a CSV log file.
///
/// Usage: logview <file.csv> [--level WARN] [--source auth] [--grep timeout]

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "analyzer/filter.h"
#include "analyzer/log_entry.h"
#include "csv/parser.h"

static void print_usage(const char* prog) {
  fprintf(
      stderr,
      "Usage: %s <file.csv> [options]\n"
      "Options:\n"
      "  --level <LEVEL>    Minimum severity (DEBUG|INFO|WARN|ERROR|FATAL)\n"
      "  --source <STR>     Filter by source (substring match)\n"
      "  --grep <STR>       Filter by message (substring match)\n"
      "  --limit <N>        Show at most N entries\n",
      prog);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  const char* filename = argv[1];
  analyzer::Severity min_level = analyzer::Severity::DEBUG;
  std::string source_filter;
  std::string message_filter;
  int limit = -1;

  // Parse args.
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
      min_level = analyzer::severity_from_string(argv[++i]);
    } else if (std::strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
      source_filter = argv[++i];
    } else if (std::strcmp(argv[i], "--grep") == 0 && i + 1 < argc) {
      message_filter = argv[++i];
    } else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
      limit = std::atoi(argv[++i]);
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  // Load and parse.
  csv::Table table;
  try {
    table = csv::parse_file(filename);
  } catch (const std::exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }

  auto entries = analyzer::parse_entries(table);

  // Build filter chain.
  std::vector<analyzer::Predicate> preds;
  preds.push_back(analyzer::min_severity(min_level));
  if (!source_filter.empty())
    preds.push_back(analyzer::source_contains(source_filter));
  if (!message_filter.empty())
    preds.push_back(analyzer::message_contains(message_filter));

  auto filtered = analyzer::filter(entries, analyzer::all_of(std::move(preds)));

  // Print results.
  int count = 0;
  for (const auto& e : filtered) {
    if (limit >= 0 && count >= limit)
      break;

    printf("[%s] %-5s %-12s %s", e.timestamp.c_str(),
           analyzer::severity_to_string(e.severity), e.source.c_str(),
           e.message.c_str());
    if (e.response_ms >= 0)
      printf(" (%lldms)", static_cast<long long>(e.response_ms));
    printf("\n");
    ++count;
  }

  fprintf(stderr, "\n%d entries shown (of %zu total, %zu matched)\n", count,
          entries.size(), filtered.size());

  return 0;
}
