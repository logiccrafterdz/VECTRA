// Latency measurement: steady_clock durations only, never cross-clock math.
//
// Durations are measured exclusively with std::chrono::steady_clock. Source
// timestamps (provider clock) and local receive timestamps are never
// subtracted from each other: the two clock domains are unrelated and no
// synchronization model exists between them in v0.1.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vectra::telemetry {

class Stopwatch {
 public:
  Stopwatch() : start_(std::chrono::steady_clock::now()) {}

  void restart() { start_ = std::chrono::steady_clock::now(); }

  std::int64_t elapsed_ns() const {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_).count();
  }

 private:
  std::chrono::steady_clock::time_point start_;
};

struct QuantileStats {
  std::size_t count = 0;
  std::size_t min_samples_required = 0;
  bool available = false;  // false => not enough samples to report honestly
  double median_ns = 0.0;
  double p95_ns = 0.0;
  double p99_ns = 0.0;
  double mean_ns = 0.0;
  double min_ns = 0.0;
  double max_ns = 0.0;
};

// Nearest-rank quantile over an ascending-sorted vector (no interpolation, so
// the reported value is always a real observed sample).
inline double nearest_rank(const std::vector<std::int64_t>& sorted, double q) {
  if (sorted.empty()) {
    return 0.0;
  }
  if (q <= 0.0) {
    return static_cast<double>(sorted.front());
  }
  if (q >= 1.0) {
    return static_cast<double>(sorted.back());
  }
  const double n = static_cast<double>(sorted.size());
  auto rank = static_cast<std::size_t>(q * n);
  if (static_cast<double>(rank) < q * n) {
    ++rank;  // ceil
  }
  if (rank == 0) {
    rank = 1;
  }
  if (rank > sorted.size()) {
    rank = sorted.size();
  }
  return static_cast<double>(sorted[rank - 1]);
}

class LatencyRecorder {
 public:
  explicit LatencyRecorder(std::size_t min_samples = 30) : min_samples_(min_samples) {}

  void set_min_samples(std::size_t v) { min_samples_ = v; }
  void record(std::int64_t duration_ns) { samples_.push_back(duration_ns); }
  void clear() { samples_.clear(); }

  std::size_t count() const { return samples_.size(); }
  const std::vector<std::int64_t>& samples() const { return samples_; }

  QuantileStats stats() const {
    QuantileStats out;
    out.count = samples_.size();
    out.min_samples_required = min_samples_;
    out.available = samples_.size() >= min_samples_ && !samples_.empty();
    if (samples_.empty()) {
      return out;
    }
    std::vector<std::int64_t> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    out.min_ns = static_cast<double>(sorted.front());
    out.max_ns = static_cast<double>(sorted.back());
    double sum = 0.0;
    for (const auto v : sorted) {
      sum += static_cast<double>(v);
    }
    out.mean_ns = sum / static_cast<double>(sorted.size());
    out.median_ns = nearest_rank(sorted, 0.50);
    out.p95_ns = nearest_rank(sorted, 0.95);
    out.p99_ns = nearest_rank(sorted, 0.99);
    return out;
  }

 private:
  std::size_t min_samples_;
  std::vector<std::int64_t> samples_;
};

}  // namespace vectra::telemetry
