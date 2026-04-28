#include "stats/summary.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace stats {

void Summary::add(double value) {
  ++count_;
  sum_ += value;

  if (count_ == 1) {
    min_ = max_ = value;
    mean_ = value;
    m2_ = 0;
    return;
  }

  min_ = std::min(min_, value);
  max_ = std::max(max_, value);

  // Welford's online algorithm for mean and variance.
  double delta = value - mean_;
  mean_ += delta / static_cast<double>(count_);
  double delta2 = value - mean_;
  m2_ += delta * delta2;
}

double Summary::variance() const {
  if (count_ < 2)
    return 0.0;
  return m2_ / static_cast<double>(count_ - 1);
}

double Summary::stddev() const {
  return std::sqrt(variance());
}

std::string Summary::to_string() const {
  if (count_ == 0)
    return "(no data)";

  std::ostringstream out;
  out.precision(2);
  out << std::fixed;
  out << "n=" << count_ << " min=" << min_ << " max=" << max_
      << " mean=" << mean_ << " stddev=" << stddev() << " sum=" << sum_;
  return out.str();
}

}  // namespace stats
