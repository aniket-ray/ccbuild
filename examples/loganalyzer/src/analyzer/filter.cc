#include "analyzer/filter.h"

#include <algorithm>

namespace analyzer {

Predicate min_severity(Severity level) {
  return [level](const LogEntry& e) {
    return static_cast<int>(e.severity) >= static_cast<int>(level);
  };
}

Predicate source_contains(const std::string& substring) {
  return [substring](const LogEntry& e) {
    return e.source.find(substring) != std::string::npos;
  };
}

Predicate message_contains(const std::string& substring) {
  return [substring](const LogEntry& e) {
    return e.message.find(substring) != std::string::npos;
  };
}

Predicate all_of(std::vector<Predicate> predicates) {
  return [preds = std::move(predicates)](const LogEntry& e) {
    return std::all_of(preds.begin(), preds.end(),
                       [&e](const Predicate& p) { return p(e); });
  };
}

std::vector<LogEntry> filter(const std::vector<LogEntry>& entries,
                             const Predicate& pred) {
  std::vector<LogEntry> result;
  std::copy_if(entries.begin(), entries.end(), std::back_inserter(result),
               pred);
  return result;
}

}  // namespace analyzer
