#ifndef LOGANALYZER_ANALYZER_FILTER_H
#define LOGANALYZER_ANALYZER_FILTER_H

#include <functional>
#include <string>
#include <vector>

#include "analyzer/log_entry.h"

namespace analyzer {

/// A filter predicate over log entries.
using Predicate = std::function<bool(const LogEntry&)>;

/// Filter entries by minimum severity level.
Predicate min_severity(Severity level);

/// Filter entries whose source matches a substring.
Predicate source_contains(const std::string& substring);

/// Filter entries whose message matches a substring.
Predicate message_contains(const std::string& substring);

/// Combine predicates with AND.
Predicate all_of(std::vector<Predicate> predicates);

/// Apply a filter to a list of entries, returning matching entries.
std::vector<LogEntry> filter(const std::vector<LogEntry>& entries,
                             const Predicate& pred);

}  // namespace analyzer

#endif  // LOGANALYZER_ANALYZER_FILTER_H
