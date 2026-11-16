// Session calendar tests: New York DST rule, blackout boundaries, late
// evaluation dropping and pre-blackout entry/exit handling in the replay.
#include "test_util.hpp"
#include "vectra_test.hpp"

#include <sstream>

#include "vectra/backtest/replay_engine.hpp"
#include "vectra/market/session.hpp"

using vectra::backtest::ReplayConfig;
using vectra::backtest::ReplayEngine;
using vectra::backtest::ReplayStats;
using vectra::market::in_new_york_blackout;
using vectra::market::new_york_utc_offset_ns;
using vectra::test::make_walk;
using vectra::test::to_csv;

namespace {

constexpr std::int64_t kHourNs = 3'600'000'000'000LL;
constexpr int kStart = 16 * 60 + 45;  // 16:45 New York
constexpr int kEnd = 18 * 60 + 30;    // 18:30 New York

// UTC epoch nanoseconds (computed independently with Python datetime).
constexpr std::int64_t k20230312_065959 = 1678604399000000000LL;
constexpr std::int64_t k20230312_070000 = 1678604400000000000LL;
constexpr std::int64_t k20231105_055959 = 1699163999000000000LL;
constexpr std::int64_t k20231105_060000 = 1699164000000000000LL;
constexpr std::int64_t k20230110_214000 = 1673386800000000000LL;  // Tue, NY 16:40 EST
constexpr std::int64_t k20230110_214459 = 1673387099000000000LL;
constexpr std::int64_t k20230110_214500 = 1673387100000000000LL;
constexpr std::int64_t k20230110_232959 = 1673393399000000000LL;
constexpr std::int64_t k20230110_233000 = 1673393400000000000LL;
constexpr std::int64_t k20230314_204459 = 1678826699000000000LL;  // Tue, NY 16:44 EDT
constexpr std::int64_t k20230314_204500 = 1678826700000000000LL;
constexpr std::int64_t k20230314_222959 = 1678832999000000000LL;
constexpr std::int64_t k20230314_223000 = 1678833000000000000LL;
constexpr std::int64_t k20230113_214459 = 1673646299000000000LL;  // Fri
constexpr std::int64_t k20230113_214500 = 1673646300000000000LL;
constexpr std::int64_t k20230114_120000 = 1673697600000000000LL;  // Sat
constexpr std::int64_t k20230115_232959 = 1673825399000000000LL;  // Sun
constexpr std::int64_t k20230115_233000 = 1673825400000000000LL;

ReplayConfig session_config() {
  ReplayConfig config;
  config.instrument = "EURUSD";
  config.validation.require_sequence = true;
  config.validation.stale_gap_threshold_ns = 15'000'000'000LL;
  config.behavior.window_short_events = 4;
  config.behavior.window_mid_events = 12;
  config.behavior.window_long_events = 24;
  config.behavior.vol_window_events = 64;
  config.behavior.persistence_window_events = 8;
  config.behavior.min_vol_samples = 4;
  config.min_latency_samples = 5;
  config.broker.reject_probability = 0.0;
  config.broker.partial_fill_probability = 0.0;
  config.broker.seed = 11;
  return config;
}

std::vector<vectra::market::MarketEvent> uptrend(std::size_t count, std::int64_t start_ns) {
  return make_walk(count, 1.10, 0.00010, 3e-5, 1.2e-4, start_ns);
}

}  // namespace

VT_TEST(Session, NewYorkOffsetFollowsUsDstRule) {
  VT_ASSERT_EQ(new_york_utc_offset_ns(k20230312_065959), -5 * kHourNs);
  VT_ASSERT_EQ(new_york_utc_offset_ns(k20230312_070000), -4 * kHourNs);
  VT_ASSERT_EQ(new_york_utc_offset_ns(k20231105_055959), -4 * kHourNs);
  VT_ASSERT_EQ(new_york_utc_offset_ns(k20231105_060000), -5 * kHourNs);
}

VT_TEST(Session, DailyBlackoutTracksNewYorkTimeInWinterAndSummer) {
  // Winter (EST): 16:45-18:30 New York = 21:45-23:30 UTC.
  VT_ASSERT_TRUE(!in_new_york_blackout(k20230110_214459, kStart, kEnd));
  VT_ASSERT_TRUE(in_new_york_blackout(k20230110_214500, kStart, kEnd));
  VT_ASSERT_TRUE(in_new_york_blackout(k20230110_232959, kStart, kEnd));
  VT_ASSERT_TRUE(!in_new_york_blackout(k20230110_233000, kStart, kEnd));
  // Summer (EDT, after 2023-03-12): the same local window is one hour earlier in UTC.
  VT_ASSERT_TRUE(!in_new_york_blackout(k20230314_204459, kStart, kEnd));
  VT_ASSERT_TRUE(in_new_york_blackout(k20230314_204500, kStart, kEnd));
  VT_ASSERT_TRUE(in_new_york_blackout(k20230314_222959, kStart, kEnd));
  VT_ASSERT_TRUE(!in_new_york_blackout(k20230314_223000, kStart, kEnd));
}

VT_TEST(Session, WeekendBlackoutRunsFromFridayCloseToSundayReopen) {
  VT_ASSERT_TRUE(!in_new_york_blackout(k20230113_214459, kStart, kEnd));
  VT_ASSERT_TRUE(in_new_york_blackout(k20230113_214500, kStart, kEnd));
  VT_ASSERT_TRUE(in_new_york_blackout(k20230114_120000, kStart, kEnd));
  VT_ASSERT_TRUE(in_new_york_blackout(k20230115_232959, kStart, kEnd));
  VT_ASSERT_TRUE(!in_new_york_blackout(k20230115_233000, kStart, kEnd));
}

namespace {

// An uptrend, a quiet gap of `gap_ns`, then a second uptrend shifted up by
// `jump` (a price move that happens while no quotes arrive).
std::string gapped_stream(std::int64_t start_ns, std::int64_t gap_ns, double jump) {
  auto first = uptrend(300, start_ns);
  auto second = make_walk(300, 1.10 + jump + 300 * 1.2e-4, 0.00010, 3e-5, 1.2e-4,
                          first.back().source_time_ns + gap_ns);
  for (std::size_t i = 0; i < second.size(); ++i) {
    second[i].sequence = static_cast<std::uint64_t>(first.size() + i + 1);
  }
  first.insert(first.end(), second.begin(), second.end());
  return to_csv(first);
}

ReplayConfig eval_config() {
  ReplayConfig cfg = session_config();
  cfg.eval_horizon_ns = 1'000'000'000LL;  // 1 s horizon
  cfg.prediction.horizon_events = 10;
  return cfg;
}

}  // namespace

VT_TEST(Session, AsOfEvaluationScoresThePricePrevailingAtTheTarget) {
  // 10 s of silence, during which the price jumps 100 pips. Legacy scoring
  // books that jump into every horizon that ends inside the silence; as-of
  // scoring uses the last quote before the target and books none of it.
  const std::string csv = gapped_stream(k20230110_214000 - 3 * kHourNs, 10'000'000'000LL, 0.0100);

  ReplayEngine legacy_engine(eval_config());
  std::istringstream legacy_in(csv);
  const ReplayStats legacy = legacy_engine.run(legacy_in, "legacy");

  ReplayConfig asof_cfg = eval_config();
  asof_cfg.eval_asof_target = true;
  ReplayEngine asof_engine(asof_cfg);
  std::istringstream asof_in(csv);
  const ReplayStats asof = asof_engine.run(asof_in, "asof");

  // Same evaluations, nothing dropped ...
  VT_ASSERT_EQ(asof.forward_baseline.evaluated, legacy.forward_baseline.evaluated);
  VT_ASSERT_EQ(asof.blackout_evals_dropped, static_cast<std::size_t>(0));
  // ... but the ~10 baseline horizons spanning the silence no longer carry the
  // +0.0100 jump (at least 0.05 of summed move disappears).
  VT_ASSERT_TRUE(legacy.forward_baseline.sum_signed_mid_move -
                     asof.forward_baseline.sum_signed_mid_move >
                 0.05);
}

VT_TEST(Session, EvaluationsWhoseHorizonTouchesABlackoutAreDropped) {
  // The first segment runs 21:40:00-21:40:30 UTC (Tue, winter); the second
  // starts two hours later, after the 21:45-23:30 UTC blackout. With a
  // 10-minute horizon every first-segment horizon reaches into the blackout.
  const std::string csv = gapped_stream(k20230110_214000, 2 * kHourNs, 0.0);

  ReplayConfig base = eval_config();
  base.eval_horizon_ns = 600'000'000'000LL;  // 10 min
  base.eval_asof_target = true;
  ReplayEngine base_engine(base);
  std::istringstream base_in(csv);
  const ReplayStats without = base_engine.run(base_in, "no-calendar");

  ReplayConfig cal = base;
  cal.session_blackout = true;
  cal.blackout_ny_start_min = kStart;
  cal.blackout_ny_end_min = kEnd;
  ReplayEngine cal_engine(cal);
  std::istringstream cal_in(csv);
  const ReplayStats with = cal_engine.run(cal_in, "calendar");

  VT_ASSERT_EQ(without.blackout_evals_dropped, static_cast<std::size_t>(0));
  VT_ASSERT_TRUE(with.blackout_evals_dropped > 0);
  VT_ASSERT_EQ(with.forward_baseline.evaluated + with.forward_all.evaluated +
                   with.blackout_evals_dropped,
               without.forward_baseline.evaluated + without.forward_all.evaluated);
}

VT_TEST(Session, EntriesAreBlockedInsideTheEntryLead) {
  // 21:40:00 UTC in winter is five minutes before the 16:45 New York blackout.
  ReplayConfig cfg = session_config();
  cfg.session_blackout = true;
  cfg.blackout_ny_start_min = kStart;
  cfg.blackout_ny_end_min = kEnd;
  cfg.blackout_entry_lead_ns = 300'000'000'000LL;
  cfg.risk.cooldown_events = 100;
  ReplayEngine engine(cfg);
  std::istringstream in(to_csv(uptrend(600, k20230110_214000)));
  const ReplayStats stats = engine.run(in, "entry-lead");

  VT_ASSERT_TRUE(stats.session_entries_blocked > 0);
  VT_ASSERT_EQ(stats.orders_submitted, static_cast<std::size_t>(0));
  VT_ASSERT_TRUE(stats.decision_reasons.count("SessionBlackout") > 0);
}

VT_TEST(Session, OpenPositionsAreFlattenedBeforeTheBlackout) {
  // Trading starts at 21:40 UTC; with a 2-minute flatten lead, positions must
  // be closed from 21:43 UTC even though the hold horizon is one hour.
  ReplayConfig cfg = session_config();
  cfg.session_blackout = true;
  cfg.blackout_ny_start_min = kStart;
  cfg.blackout_ny_end_min = kEnd;
  cfg.blackout_entry_lead_ns = 0;
  cfg.blackout_flatten_lead_ns = 120'000'000'000LL;
  cfg.hold_horizon_ns = kHourNs;
  cfg.risk.cooldown_events = 100;
  ReplayEngine engine(cfg);
  // 2400 quotes at 100 ms = 21:40:00 to 21:44:00 UTC.
  std::istringstream in(to_csv(uptrend(2400, k20230110_214000)));
  const ReplayStats stats = engine.run(in, "flatten");

  VT_ASSERT_TRUE(stats.session_exits_submitted > 0);
  VT_ASSERT_EQ(stats.horizon_exits_submitted, static_cast<std::size_t>(0));
}

VT_TEST(Session, DisabledCalendarLeavesLegacyReplayUnchanged) {
  const std::string csv = to_csv(uptrend(600, k20230110_214000));
  ReplayConfig base = session_config();
  base.risk.cooldown_events = 100;
  ReplayEngine a(base);
  std::istringstream in_a(csv);
  const ReplayStats sa = a.run(in_a, "a");

  ReplayConfig off = base;
  off.session_blackout = false;
  off.blackout_entry_lead_ns = 300'000'000'000LL;  // ignored while disabled
  ReplayEngine b(off);
  std::istringstream in_b(csv);
  const ReplayStats sb = b.run(in_b, "b");

  VT_ASSERT_EQ(sa.orders_submitted, sb.orders_submitted);
  VT_ASSERT_EQ(sa.fills, sb.fills);
  VT_ASSERT_NEAR(sa.net_pnl, sb.net_pnl, 1e-12);
  VT_ASSERT_EQ(sb.session_entries_blocked, static_cast<std::size_t>(0));
}
