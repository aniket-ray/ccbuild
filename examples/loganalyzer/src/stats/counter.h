#ifndef LOGANALYZER_STATS_COUNTER_H
#define LOGANALYZER_STATS_COUNTER_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace stats {

/// Frequency counter -- counts occurrences of string keys.
class Counter {
 public:
  void add(const std::string& key);
  void add(const std::string& key, size_t n);

  [[nodiscard]] size_t get(const std::string& key) const;
  [[nodiscard]] size_t total() const { return total_; }
  [[nodiscard]] size_t unique() const { return counts_.size(); }

  /// Return entries sorted by count (descending).
  [[nodiscard]] std::vector<std::pair<std::string, size_t>> sorted() const;

  /// Return the top-N entries by count.
  [[nodiscard]] std::vector<std::pair<std::string, size_t>> top(size_t n) const;

 private:
  std::map<std::string, size_t> counts_;
  size_t total_ = 0;
};

}  // namespace stats

#endif  // LOGANALYZER_STATS_COUNTER_H
