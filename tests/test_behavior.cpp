// Behavioral engine tests: rolling windows, feature derivation, edge cases.
#include "test_util.hpp"
#include "vectra_test.hpp"

#include <cmath>

#include "vectra/behavior/behavior_engine.hpp"
#include "vectra/behavior/exhaustion.hpp"

using vectra::behavior::BehaviorEngine;
using vectra::behavior::BehaviorEngineConfig;
using vectra::behavior::BehaviorFeatures;
using vectra::behavior::compute_exhaustion;
using vectra::behavior::DataQualityFlags;
using vectra::behavior::ExhaustionConfig;
using vectra::behavior::RollingWindow;
using vectra::market::MarketEvent;
using vectra::market::Validity;
using vectra::test::make_event;
using vectra::test::make_quote;
using vectra::test::make_walk;

namespace {

BehaviorEngineConfig small_config() {
  BehaviorEngineConfig config;
  config.window_short_events = 4;
  config.window_mid_events = 12;
  config.window_long_events = 24;
  config.vol_window_events = 64;
  config.persistence_window_events = 8;
  config.min_vol_samples = 4;
  config.min_volatility = 1e-12;
  config.spread_warn_ratio = 0.001;
  return config;
}

}  // namespace

// ---------------------------------------------------------------------------
// Rolling window statistics
// ---------------------------------------------------------------------------

VT_TEST(RollingWindow, ComputesMeanAndSampleVariance) {
  RollingWindow window(8);
  for (int v = 1; v <= 5; ++v) {
    window.push(static_cast<double>(v));
  }
  VT_ASSERT_EQ(window.size(), std::size_t{5});
  VT_ASSERT_NEAR(window.mean(), 3.0, 1e-12);
  VT_ASSERT_NEAR(window.variance(), 2.5, 1e-12);  // sample variance of 1..5
  VT_ASSERT_NEAR(window.stddev(), std::sqrt(2.5), 1e-12);
}

VT_TEST(RollingWindow, EvictsOldestValues) {
  RollingWindow window(3);
  for (int v = 1; v <= 5; ++v) {
    window.push(static_cast<double>(v));
  }
  VT_ASSERT_EQ(window.size(), std::size_t{3});
  VT_ASSERT_NEAR(window.mean(), 4.0, 1e-12);   // window holds {3,4,5}
  VT_ASSERT_NEAR(window.variance(), 1.0, 1e-12);
}

VT_TEST(RollingWindow, HandlesConstantInputAndSingleSample) {
  RollingWindow window(4);
  window.push(2.5);
  VT_ASSERT_NEAR(window.variance(), 0.0, 1e-15);  // n < 2 => 0
  for (int i = 0; i < 10; ++i) {
    window.push(2.5);
  }
  VT_ASSERT_NEAR(window.mean(), 2.5, 1e-12);
  VT_ASSERT_NEAR(window.variance(), 0.0, 1e-15);
}

VT_TEST(RollingWindow, EmptiesCleanly) {
  RollingWindow window(2);
  window.push(1.0);
  window.push(2.0);
  window.push(3.0);  // evicts 1.0
  window.clear();
  VT_ASSERT_TRUE(window.empty());
  VT_ASSERT_NEAR(window.mean(), 0.0, 1e-15);
  window.push(4.0);
  VT_ASSERT_NEAR(window.mean(), 4.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Behavior features
// ---------------------------------------------------------------------------

VT_TEST(Behavior, FlagsInsufficientHistoryBeforeWindowsFill) {
  BehaviorEngine engine(small_config());
  for (std::size_t i = 0; i < 10; ++i) {  // long window needs 24 + 1 mids
    engine.update(make_quote(1.10 + 0.0001 * static_cast<double>(i), 0.00010,
                             static_cast<std::int64_t>(i + 1) * 100'000'000,
                             static_cast<std::uint64_t>(i + 1)));
  }
  const auto& features = engine.features();
  VT_ASSERT_FALSE(features.valid);
  VT_ASSERT_TRUE(features.flags.insufficient_history);
}

VT_TEST(Behavior, ComputesDisplacementsWithCorrectSign) {
  BehaviorEngine engine(small_config());
  // Deterministic monotone uptrend: every displacement must be positive.
  for (std::size_t i = 0; i < 60; ++i) {
    engine.update(make_quote(1.10 + 0.0001 * static_cast<double>(i), 0.00010,
                             static_cast<std::int64_t>(i + 1) * 100'000'000,
                             static_cast<std::uint64_t>(i + 1)));
  }
  const auto& features = engine.features();
  VT_ASSERT_TRUE(features.valid);
  VT_ASSERT_FALSE(features.flags.insufficient_history);
  VT_ASSERT_TRUE(features.disp_short > 0.0);
  VT_ASSERT_TRUE(features.disp_mid > 0.0);
  VT_ASSERT_TRUE(features.disp_long > 0.0);
  VT_ASSERT_TRUE(features.persistence > 0.9);   // every tick agreed in sign
  VT_ASSERT_TRUE(features.response_short > 0.0);
  VT_ASSERT_TRUE(std::isfinite(features.z_short));
  VT_ASSERT_EQ(engine.event_count(), std::uint64_t{60});
}

VT_TEST(Behavior, NegativeTrendProducesNegativeDisplacement) {
  BehaviorEngine engine(small_config());
  for (std::size_t i = 0; i < 60; ++i) {
    engine.update(make_quote(1.20 - 0.0001 * static_cast<double>(i), 0.00010,
                             static_cast<std::int64_t>(i + 1) * 100'000'000,
                             static_cast<std::uint64_t>(i + 1)));
  }
  const auto& features = engine.features();
  VT_ASSERT_TRUE(features.valid);
  VT_ASSERT_TRUE(features.disp_short < 0.0);
  VT_ASSERT_TRUE(features.persistence > 0.9);
}

VT_TEST(Behavior, ZeroVolatilityIsFlagged) {
  BehaviorEngineConfig config = small_config();
  config.min_volatility = 1e-8;
  BehaviorEngine engine(config);
  // Long stretch of identical quotes: every log return is exactly zero.
  for (std::size_t i = 0; i < 400; ++i) {
    engine.update(make_quote(1.10, 0.00010, static_cast<std::int64_t>(i + 1) * 100'000'000,
                             static_cast<std::uint64_t>(i + 1)));
  }
  const auto& features = engine.features();
  VT_ASSERT_TRUE(features.valid);
  VT_ASSERT_TRUE(features.flags.low_volatility);
  VT_ASSERT_NEAR(features.volatility, 0.0, 1e-15);
  VT_ASSERT_NEAR(features.z_short, 0.0, 1e-15);  // never divides by zero
  VT_ASSERT_TRUE(std::isfinite(features.deceleration));
}

VT_TEST(Behavior, WalkProducesFiniteBoundedFeatures) {
  BehaviorEngine engine(small_config());
  const auto events = make_walk(500, 1.10, 0.00010, 5e-5, 1e-5);
  for (const auto& event : events) {
    const auto& features = engine.update(event);
    if (features.valid) {
      VT_ASSERT_TRUE(std::isfinite(features.z_short));
      VT_ASSERT_TRUE(std::isfinite(features.disp_short));
      VT_ASSERT_TRUE(std::isfinite(features.persistence));
      VT_ASSERT_TRUE(features.persistence >= -1.0 && features.persistence <= 1.0);
      VT_ASSERT_TRUE(features.response_fade >= 0.0 && features.response_fade <= 1.0);
      VT_ASSERT_TRUE(features.deceleration >= -1.0 && features.deceleration <= 1.0);
    }
  }
}

VT_TEST(Behavior, InvalidEventIsNotIngested) {
  BehaviorEngine engine(small_config());
  for (std::size_t i = 0; i < 40; ++i) {
    engine.update(make_quote(1.10 + 0.0001 * static_cast<double>(i), 0.00010,
                             static_cast<std::int64_t>(i + 1) * 100'000'000,
                             static_cast<std::uint64_t>(i + 1)));
  }
  const std::size_t history_before = engine.history_size();

  MarketEvent bad = make_quote(1.11, 0.00010, 5'000'000'000, 41);
  bad.validity = Validity::WideSpread;
  const auto& features = engine.update(bad);
  VT_ASSERT_FALSE(features.valid);
  VT_ASSERT_TRUE(features.flags.invalid_event);
  VT_ASSERT_EQ(engine.history_size(), history_before);  // rolling state untouched
  VT_ASSERT_EQ(engine.event_count(), std::uint64_t{40});
}

VT_TEST(Behavior, StaleGapFlagPropagates) {
  BehaviorEngine engine(small_config());
  auto events = make_walk(50, 1.10, 0.00010, 5e-5, 1e-5);
  for (auto& event : events) {
    engine.update(event);
  }
  MarketEvent after_gap = make_quote(1.11, 0.00010, 99'000'000'000, 51);
  after_gap.flags = vectra::market::EventFlags::StaleGap;
  const auto& features = engine.update(after_gap);
  VT_ASSERT_TRUE(features.flags.stale_gap);
}

VT_TEST(Behavior, WideSpreadIsFlagged) {
  BehaviorEngineConfig config = small_config();
  config.spread_warn_ratio = 0.0002;  // 0.0005 spread exceeds it
  BehaviorEngine engine(config);
  for (std::size_t i = 0; i < 40; ++i) {
    engine.update(make_quote(1.10 + 0.0001 * static_cast<double>(i), 0.00050,
                             static_cast<std::int64_t>(i + 1) * 100'000'000,
                             static_cast<std::uint64_t>(i + 1)));
  }
  VT_ASSERT_TRUE(engine.features().flags.wide_spread);
}

VT_TEST(Behavior, WallClockTimeWindowsAreInvariantToTickRate) {
  BehaviorEngineConfig config = small_config();
  config.window_short_ns = 1'000'000'000LL;  // 1 s
  config.window_mid_ns = 2'000'000'000LL;    // 2 s
  config.window_long_ns = 3'000'000'000LL;   // 3 s
  config.min_vol_samples = 4;

  BehaviorEngine engine(config);
  // 40 ticks spaced 100 ms apart (total 4.0 s), rising 0.0001 per tick.
  for (std::size_t i = 0; i <= 40; ++i) {
    engine.update(make_quote(1.1000 + 0.0001 * static_cast<double>(i), 0.00010,
                             static_cast<std::int64_t>(i + 1) * 100'000'000LL,
                             static_cast<std::uint64_t>(i + 1)));
  }
  const auto& f = engine.features();
  VT_ASSERT_TRUE(f.valid);
  // 1 s at 100 ms/tick is 10 events back: mid moved from 1.1030 to 1.1040.
  VT_ASSERT_EQ(f.short_window_events, std::size_t{10});
  VT_ASSERT_NEAR(f.disp_short, std::log(1.1040 / 1.1030), 1e-12);
  VT_ASSERT_NEAR(f.regime_efficiency, 1.0, 1e-9);
}

VT_TEST(Behavior, RegimeEfficiencyDistinguishesTrendFromChop) {
  BehaviorEngineConfig config = small_config();
  config.window_short_events = 4;
  config.window_mid_events = 8;
  config.window_long_events = 16;

  BehaviorEngine chop_engine(config);
  // Alternating 4-tick blocks (+0.0004 over 4 ticks, then -0.0004 over 4 ticks).
  double mid = 1.1000;
  for (std::size_t i = 0; i <= 20; ++i) {
    const std::size_t block = (i / 4) % 2;
    mid += (block == 0 ? 0.0001 : -0.0001);
    chop_engine.update(make_quote(mid, 0.00010,
                                  static_cast<std::int64_t>(i + 1) * 100'000'000LL,
                                  static_cast<std::uint64_t>(i + 1)));
  }
  VT_ASSERT_TRUE(chop_engine.features().valid);
  VT_ASSERT_TRUE(chop_engine.features().regime_efficiency < 0.35);
}

// ---------------------------------------------------------------------------
// Exhaustion score edge cases
// ---------------------------------------------------------------------------

namespace {

BehaviorFeatures valid_features() {
  BehaviorFeatures features;
  features.valid = true;
  features.time_ns = 1;
  features.mid = 1.10;
  features.spread = 0.0001;
  features.spread_ratio = 0.0001 / 1.10;
  features.z_short = 2.0;
  features.persistence = 0.5;
  features.volatility = 1e-5;
  features.vol_samples = 64;
  features.short_window_events = 8;
  return features;
}

}  // namespace

VT_TEST(Exhaustion, InvalidFeaturesProduceInvalidResult) {
  BehaviorFeatures features;
  features.valid = false;
  const auto result = compute_exhaustion(features, ExhaustionConfig{});
  VT_ASSERT_FALSE(result.valid);
  VT_ASSERT_NEAR(result.score, 0.0, 1e-15);
}

VT_TEST(Exhaustion, ScoreStaysWithinUnitInterval) {
  ExhaustionConfig config;
  BehaviorFeatures features = valid_features();
  features.z_short = 1000.0;      // extension saturates
  features.deceleration = 1.0;
  features.response_fade = 1.0;
  features.flags.stale_gap = true;
  features.flags.low_activity = true;
  const auto result = compute_exhaustion(features, config);
  VT_ASSERT_TRUE(result.valid);
  VT_ASSERT_TRUE(result.score >= 0.0 && result.score <= 1.0);
  VT_ASSERT_TRUE(result.extension <= 1.0);
  VT_ASSERT_TRUE(result.quality_penalty > 0.0);
}

VT_TEST(Exhaustion, NormalSpreadContributesNoQualityPenalty) {
  ExhaustionConfig config;
  BehaviorFeatures features = valid_features();  // spread ratio ~9e-5 << reference/2
  const auto result = compute_exhaustion(features, config);
  VT_ASSERT_NEAR(result.quality_penalty, 0.0, 1e-12);
}

VT_TEST(Exhaustion, StaleGapRaisesQualityPenalty) {
  ExhaustionConfig config;
  BehaviorFeatures features = valid_features();
  features.flags.stale_gap = true;
  const auto result = compute_exhaustion(features, config);
  VT_ASSERT_TRUE(result.quality_penalty > 0.0);
}

VT_TEST(Exhaustion, ZeroWeightsDoNotDivideByZero) {
  ExhaustionConfig config;
  config.weight_extension = 0.0;
  config.weight_deceleration = 0.0;
  config.weight_response_fade = 0.0;
  config.weight_quality = 0.0;
  const auto result = compute_exhaustion(valid_features(), config);
  VT_ASSERT_TRUE(result.valid);
  VT_ASSERT_NEAR(result.score, 0.0, 1e-15);
}

VT_TEST(Exhaustion, StaticMoveScoresLowAndStoppingMoveScoresHigh) {
  ExhaustionConfig config;
  BehaviorFeatures steady = valid_features();
  steady.z_short = 3.0;
  steady.deceleration = 0.0;
  steady.response_fade = 0.0;
  const auto steady_result = compute_exhaustion(steady, config);

  BehaviorFeatures stopping = valid_features();
  stopping.z_short = 3.0;
  stopping.deceleration = 1.0;
  stopping.response_fade = 1.0;
  const auto stopping_result = compute_exhaustion(stopping, config);

  VT_ASSERT_TRUE(stopping_result.score > steady_result.score);
}
