#ifndef LOGANALYZER_ANALYZER_LOG_ENTRY_H
#define LOGANALYZER_ANALYZER_LOG_ENTRY_H

#include <cstdint>
#include <string>
#include <vector>

#include "csv/parser.h"

namespace analyzer {

enum class Severity { DEBUG, INFO, WARN, ERROR, FATAL, UNKNOWN };

/// Convert between severity and string.
Severity severity_from_string(const std::string& s);
const char* severity_to_string(Severity s);

/// A parsed log entry.
struct LogEntry {
  std::string timestamp;
  Severity severity = Severity::UNKNOWN;
  std::string source;  // e.g. module/component name
  std::string message;
  int64_t response_ms = -1;  // optional numeric field (e.g. response time)
};

/// Parse a CSV table into log entries.
/// Expects columns: timestamp, severity, source, message, response_ms
std::vector<LogEntry> parse_entries(const csv::Table& table);

}  // namespace analyzer

#endif  // LOGANALYZER_ANALYZER_LOG_ENTRY_H
