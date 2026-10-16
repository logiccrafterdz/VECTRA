// Telemetry tests: quantile math, sample-count gating, steady_clock timing.
#include "vectra_test.hpp"

#include <vector>

#include "vectra/telemetry/latency.hpp"

using vectra::telemetry::LatencyRecorder;
using vectra::telemetry::nearest_rank;
using vectra::telemetry::QuantileStats;
using vectra::telemetry::Stopwatch;

VT_TEST(Telemetry, NearestRankQuantilesOnKnownSample) {
  std::vector<std::int64_t> sorted;
  for (std::int64_t i = 1; i <= 100; ++i) {
    sorted.push_back(i * 1000);
  }
  VT_ASSERT_NEAR(nearest_rank(sorted, 0.50), 50'000.0, 1e-9);
  VT_ASSERT_NEAR(nearest_rank(sorted, 0.95), 95'000.0, 1e-9);
  VT_ASSERT_NEAR(nearest_rank(sorted, 0.99), 99'000.0, 1e-9);
  VT_ASSERT_NEAR(nearest_rank(sorted, 0.0), 1'000.0, 1e-9);
  VT_ASSERT_NEAR(nearest_rank(sorted, 1.0), 100'000.0, 1e-9);
}

VT_TEST(Telemetry, SmallSampleIsReportedAsUnavailable) {
  LatencyRecorder recorder(30);
  for (std::int64_t i = 0; i < 10; ++i) {
    recorder.record(1000 + i);
  }
  const QuantileStats stats = recorder.stats();
  VT_ASSERT_FALSE(stats.available);
  VT_ASSERT_EQ(stats.count, std::size_t{10});
  VT_ASSERT_EQ(stats.min_samples_required, std::size_t{30});
}

VT_TEST(Telemetry, EnoughSamplesBecomeAvailable) {
  LatencyRecorder recorder(30);
  for (std::int64_t i = 1; i <= 100; ++i) {
    recorder.record(i * 100);
  }
  const QuantileStats stats = recorder.stats();
  VT_ASSERT_TRUE(stats.available);
  VT_ASSERT_EQ(stats.count, std::size_t{100});
  VT_ASSERT_NEAR(stats.median_ns, 5000.0, 1e-9);
  VT_ASSERT_NEAR(stats.p95_ns, 9500.0, 1e-9);
  VT_ASSERT_NEAR(stats.p99_ns, 9900.0, 1e-9);
  VT_ASSERT_NEAR(stats.min_ns, 100.0, 1e-9);
  VT_ASSERT_NEAR(stats.max_ns, 10'000.0, 1e-9);
  VT_ASSERT_NEAR(stats.mean_ns, 5050.0, 1e-9);
}

VT_TEST(Telemetry, EmptyRecorderReportsNothing) {
  LatencyRecorder recorder(1);
  const QuantileStats stats = recorder.stats();
  VT_ASSERT_FALSE(stats.available);
  VT_ASSERT_EQ(stats.count, std::size_t{0});
}

VT_TEST(Telemetry, StopwatchMeasuresWithSteadyClock) {
  Stopwatch stopwatch;
  volatile double sink = 0.0;
  for (int i = 0; i < 100000; ++i) {
    sink += 1.0;
  }
  (void)sink;
  const std::int64_t elapsed = stopwatch.elapsed_ns();
  VT_ASSERT_TRUE(elapsed >= 0);
  stopwatch.restart();
  VT_ASSERT_TRUE(stopwatch.elapsed_ns() >= 0);
}

VT_TEST(Telemetry, UnsortedInputIsSortedBeforeQuantiles) {
  LatencyRecorder recorder(3);
  recorder.record(9000);
  recorder.record(1000);
  recorder.record(5000);
  recorder.record(3000);
  const QuantileStats stats = recorder.stats();
  VT_ASSERT_TRUE(stats.available);
  VT_ASSERT_NEAR(stats.median_ns, 3000.0, 1e-9);  // nearest rank on sorted data
  VT_ASSERT_NEAR(stats.min_ns, 1000.0, 1e-9);
  VT_ASSERT_NEAR(stats.max_ns, 9000.0, 1e-9);
}
