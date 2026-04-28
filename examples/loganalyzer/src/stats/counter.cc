#include "stats/counter.h"

#include <algorithm>

namespace stats {

void Counter::add(const std::string& key) {
  ++counts_[key];
  ++total_;
}

void Counter::add(const std::string& key, size_t n) {
  counts_[key] += n;
  total_ += n;
}

size_t Counter::get(const std::string& key) const {
  auto it = counts_.find(key);
  return (it != counts_.end()) ? it->second : 0;
}

std::vector<std::pair<std::string, size_t>> Counter::sorted() const {
  std::vector<std::pair<std::string, size_t>> result(counts_.begin(),
                                                     counts_.end());
  std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  return result;
}

std::vector<std::pair<std::string, size_t>> Counter::top(size_t n) const {
  auto all = sorted();
  if (all.size() > n)
    all.resize(n);
  return all;
}

}  // namespace stats
