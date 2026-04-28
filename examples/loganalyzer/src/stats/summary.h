#ifndef LOGANALYZER_STATS_SUMMARY_H
#define LOGANALYZER_STATS_SUMMARY_H

#include <cstddef>
#include <string>

namespace stats {

/// Running statistics accumulator (online algorithm).
/// Computes count, min, max, mean, variance without storing all values.
class Summary {
 public:
  void add(double value);

  [[nodiscard]] size_t count() const { return count_; }
  [[nodiscard]] double min() const { return min_; }
  [[nodiscard]] double max() const { return max_; }
  [[nodiscard]] double mean() const { return mean_; }
  [[nodiscard]] double variance() const;
  [[nodiscard]] double stddev() const;
  [[nodiscard]] double sum() const { return sum_; }

  /// Format as a human-readable string.
  [[nodiscard]] std::string to_string() const;

 private:
  size_t count_ = 0;
  double min_ = 0;
  double max_ = 0;
  double mean_ = 0;
  double m2_ = 0;  // for Welford's online variance
  double sum_ = 0;
};

}  // namespace stats

#endif  // LOGANALYZER_STATS_SUMMARY_H
