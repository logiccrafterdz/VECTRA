// Rolling statistics over a bounded window.
//
// Uses Welford's online algorithm with exact element removal so that mean and
// variance stay numerically stable while the window slides. Sample variance
// (divided by n-1) is used when at least two samples are present, otherwise 0.
#pragma once

#include <cmath>
#include <cstddef>
#include <deque>

namespace vectra::behavior {

class RollingWindow {
 public:
  explicit RollingWindow(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

  void push(double value) {
    values_.push_back(value);
    const double n = static_cast<double>(values_.size());
    const double delta = value - mean_;
    mean_ += delta / n;
    m2_ += delta * (value - mean_);
    while (values_.size() > capacity_) {
      remove_oldest();
    }
  }

  void clear() {
    values_.clear();
    mean_ = 0.0;
    m2_ = 0.0;
  }

  std::size_t size() const { return values_.size(); }
  std::size_t capacity() const { return capacity_; }
  bool empty() const { return values_.empty(); }
  bool full() const { return values_.size() >= capacity_; }

  double mean() const { return values_.empty() ? 0.0 : mean_; }

  double variance() const {
    const std::size_t n = values_.size();
    if (n < 2) {
      return 0.0;
    }
    const double divisor = static_cast<double>(n - 1);
    const double v = m2_ / divisor;
    return v > 0.0 ? v : 0.0;  // guard tiny negative values from round-off
  }

  double stddev() const { return std::sqrt(variance()); }

  const std::deque<double>& values() const { return values_; }

 private:
  void remove_oldest() {
    const double x = values_.front();
    values_.pop_front();
    const double n_old = static_cast<double>(values_.size() + 1);  // size before removal
    if (values_.empty()) {
      mean_ = 0.0;
      m2_ = 0.0;
      return;
    }
    const double n_new = static_cast<double>(values_.size());
    const double new_mean = (mean_ * n_old - x) / n_new;
    m2_ -= (x - mean_) * (x - new_mean);
    if (m2_ < 0.0) {
      m2_ = 0.0;
    }
    mean_ = new_mean;
  }

  std::size_t capacity_;
  std::deque<double> values_;
  double mean_ = 0.0;
  double m2_ = 0.0;
};

}  // namespace vectra::behavior
