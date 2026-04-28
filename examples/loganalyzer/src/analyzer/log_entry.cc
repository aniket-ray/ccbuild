#include "analyzer/log_entry.h"

#include <algorithm>
#include <stdexcept>

namespace analyzer {

Severity severity_from_string(const std::string& s) {
  // Case-insensitive match.
  std::string upper = s;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  if (upper == "DEBUG")
    return Severity::DEBUG;
  if (upper == "INFO")
    return Severity::INFO;
  if (upper == "WARN" || upper == "WARNING")
    return Severity::WARN;
  if (upper == "ERROR" || upper == "ERR")
    return Severity::ERROR;
  if (upper == "FATAL" || upper == "CRITICAL")
    return Severity::FATAL;
  return Severity::UNKNOWN;
}

const char* severity_to_string(Severity s) {
  switch (s) {
  case Severity::DEBUG:
    return "DEBUG";
  case Severity::INFO:
    return "INFO";
  case Severity::WARN:
    return "WARN";
  case Severity::ERROR:
    return "ERROR";
  case Severity::FATAL:
    return "FATAL";
  case Severity::UNKNOWN:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

/// Find column index by name (case-insensitive). Returns -1 if not found.
static int find_column(const csv::Row& header, const std::string& name) {
  std::string target = name;
  std::transform(target.begin(), target.end(), target.begin(), ::tolower);

  for (size_t i = 0; i < header.size(); ++i) {
    std::string col = header[i];
    std::transform(col.begin(), col.end(), col.begin(), ::tolower);
    if (col == target)
      return static_cast<int>(i);
  }
  return -1;
}

std::vector<LogEntry> parse_entries(const csv::Table& table) {
  // Find columns by name.
  int ts_col = find_column(table.header, "timestamp");
  int sev_col = find_column(table.header, "severity");
  int src_col = find_column(table.header, "source");
  int msg_col = find_column(table.header, "message");
  int resp_col = find_column(table.header, "response_ms");

  if (ts_col < 0 || sev_col < 0 || msg_col < 0) {
    throw std::runtime_error(
        "CSV must have at minimum: timestamp, severity, message columns");
  }

  std::vector<LogEntry> entries;
  entries.reserve(table.rows.size());

  for (const auto& row : table.rows) {
    LogEntry entry;

    auto field = [&](int col) -> std::string {
      if (col < 0 || static_cast<size_t>(col) >= row.size())
        return "";
      return row[static_cast<size_t>(col)];
    };

    entry.timestamp = field(ts_col);
    entry.severity = severity_from_string(field(sev_col));
    entry.source = field(src_col);
    entry.message = field(msg_col);

    std::string resp_str = field(resp_col);
    if (!resp_str.empty()) {
      try {
        entry.response_ms = std::stoll(resp_str);
      } catch (...) {
        entry.response_ms = -1;
      }
    }

    entries.push_back(std::move(entry));
  }

  return entries;
}

}  // namespace analyzer
