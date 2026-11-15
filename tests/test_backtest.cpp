// Replay engine tests: no look-ahead, deterministic replay, anomaly handling,
// risk integration and report content.
#include "test_util.hpp"
#include "vectra_test.hpp"

#include <sstream>

#include "vectra/backtest/replay_engine.hpp"
#include "vectra/backtest/report.hpp"

using vectra::backtest::ReplayConfig;
using vectra::backtest::ReplayEngine;
using vectra::backtest::ReplayStats;
using vectra::prediction::RejectionReason;
using vectra::prediction::SignalKind;
using vectra::test::make_quote;
using vectra::test::make_walk;
using vectra::test::to_csv;

namespace {

ReplayConfig test_config() {
  ReplayConfig config;
  config.instrument = "EURUSD";
  config.validation.require_sequence = true;
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

// Strong, persistent uptrend: reliably produces continuation candidates.
std::vector<vectra::market::MarketEvent> trending_walk(std::size_t count) {
  return make_walk(count, 1.10, 0.00010, 3e-5, 1.2e-4);
}

// Deterministic shape: a short climb (continuation longs fill) followed by a
// long sell-off (continuation shorts close those longs, realizing P&L) and a
// flat tail. This realizes a NET LOSS, so the daily-loss gate is exercised.
std::vector<vectra::market::MarketEvent> v_walk() {
  std::vector<vectra::market::MarketEvent> events;
  std::int64_t ts = 0;
  std::uint64_t seq = 1;
  double mid = 1.1000;
  for (int i = 0; i < 40; ++i) {  // climb to ~1.1040
    mid += 0.0001;
    ts += 100'000'000;
    events.push_back(make_quote(mid, 0.00010, ts, seq++));
  }
  for (int i = 0; i < 300; ++i) {  // sustained sell-off to ~1.0740
    mid -= 0.0001;
    ts += 100'000'000;
    events.push_back(make_quote(mid, 0.00010, ts, seq++));
  }
  for (int i = 0; i < 40; ++i) {  // flat tail: post-loss decisions still occur
    ts += 100'000'000;
    events.push_back(make_quote(mid, 0.00010, ts, seq++));
  }
  return events;
}

}  // namespace

// ---------------------------------------------------------------------------
// No look-ahead: results for a prefix must be identical whether or not the
// stream continues afterwards.
// ---------------------------------------------------------------------------

VT_TEST(Replay, NoLookAheadOnPrefix) {
  const auto events = trending_walk(400);
  const std::string full_csv = to_csv(events);
  const std::string prefix_csv =
      to_csv(std::vector<vectra::market::MarketEvent>(events.begin(), events.begin() + 200));

  ReplayEngine full_engine(test_config());
  std::istringstream full_in(full_csv);
  full_engine.run(full_in, "full");

  ReplayEngine prefix_engine(test_config());
  std::istringstream prefix_in(prefix_csv);
  prefix_engine.run(prefix_in, "prefix");

  const auto& full_records = full_engine.signal_records();
  const auto& prefix_records = prefix_engine.signal_records();
  VT_ASSERT_TRUE(prefix_records.size() > 0);
  VT_ASSERT_TRUE(prefix_records.size() <= full_records.size());

  for (std::size_t i = 0; i < prefix_records.size(); ++i) {
    VT_ASSERT_EQ(full_records[i].time_ns, prefix_records[i].time_ns);
    VT_ASSERT_EQ(static_cast<int>(full_records[i].kind),
                 static_cast<int>(prefix_records[i].kind));
    VT_ASSERT_EQ(static_cast<int>(full_records[i].rejection),
                 static_cast<int>(prefix_records[i].rejection));
    VT_ASSERT_NEAR(full_records[i].exhaustion, prefix_records[i].exhaustion, 0.0);
    VT_ASSERT_NEAR(full_records[i].expected_move_log, prefix_records[i].expected_move_log, 0.0);
    VT_ASSERT_NEAR(full_records[i].z_short, prefix_records[i].z_short, 0.0);
  }
}

// ---------------------------------------------------------------------------
// Determinism: identical input => identical run fingerprint and results.
// ---------------------------------------------------------------------------

VT_TEST(Replay, DeterministicReplayProducesIdenticalDigest) {
  const std::string csv = to_csv(trending_walk(400));

  ReplayEngine engine_a(test_config());
  std::istringstream in_a(csv);
  const ReplayStats stats_a = engine_a.run(in_a, "a");

  ReplayEngine engine_b(test_config());
  std::istringstream in_b(csv);
  const ReplayStats stats_b = engine_b.run(in_b, "b");

  VT_ASSERT_EQ(stats_a.digest, stats_b.digest);
  VT_ASSERT_EQ(stats_a.events_valid, stats_b.events_valid);
  VT_ASSERT_EQ(stats_a.signals, stats_b.signals);
  VT_ASSERT_EQ(stats_a.fills, stats_b.fills);
  VT_ASSERT_NEAR(stats_a.net_pnl, stats_b.net_pnl, 0.0);
  VT_ASSERT_NEAR(stats_a.max_drawdown, stats_b.max_drawdown, 0.0);
  VT_ASSERT_EQ(engine_a.signal_records().size(), engine_b.signal_records().size());
}

// ---------------------------------------------------------------------------
// Anomalous input is counted, never silently swallowed.
// ---------------------------------------------------------------------------

VT_TEST(Replay, CountsEveryAnomalyClass) {
  const std::string csv =
      "symbol,bid,ask,source_ts_ns,sequence\n"
      "# comment line\n"
      "EURUSD,1.10000,1.10010,1000000000,1\n"      // 1: valid
      "EURUSD,1.10100,1.10000,2000000000,2\n"      // 2: crossed market
      "EURUSD,not-a-price,1.10010,3000000000,3\n"  // 3: malformed
      "GBPUSD,1.10000,1.10010,4000000000,4\n"      // 4: wrong symbol
      "EURUSD,1.10020,1.10030,4500000000,5\n"      // 5: valid
      "EURUSD,1.10040,1.10050,5500000000,5\n"      // 6: duplicate sequence
      "EURUSD,1.10060,1.10070,3000000000,7\n"      // 7: out-of-order timestamp
      "EURUSD,1.10080,1.10090,15000000000,8\n";    // 8: valid, 10.5 s gap => stale flag

  ReplayEngine engine(test_config());
  std::istringstream input(csv);
  const ReplayStats stats = engine.run(input, "anomalies");

  VT_ASSERT_EQ(stats.lines_read, std::size_t{8});
  VT_ASSERT_EQ(stats.events_malformed, std::size_t{1});
  VT_ASSERT_EQ(stats.events_parsed, std::size_t{7});
  VT_ASSERT_EQ(stats.events_valid, std::size_t{3});
  VT_ASSERT_EQ(stats.events_rejected, std::size_t{4});
  VT_ASSERT_EQ(stats.stale_gaps, std::size_t{1});
  VT_ASSERT_EQ(stats.invalid_reasons.at("NegativeSpread"), 1);
  VT_ASSERT_EQ(stats.invalid_reasons.at("WrongSymbol"), 1);
  VT_ASSERT_EQ(stats.invalid_reasons.at("Duplicate"), 1);
  VT_ASSERT_EQ(stats.invalid_reasons.at("OutOfOrder"), 1);
  VT_ASSERT_EQ(engine.signal_records().size(), stats.events_valid);
  VT_ASSERT_TRUE(stats.reconciliation.matched);
}

// ---------------------------------------------------------------------------
// Risk integration
// ---------------------------------------------------------------------------

VT_TEST(Replay, KillSwitchBlocksAllOrders) {
  ReplayConfig config = test_config();
  config.risk.kill_switch = true;
  ReplayEngine engine(config);
  std::istringstream input(to_csv(trending_walk(300)));
  const ReplayStats stats = engine.run(input, "kill-switch");

  VT_ASSERT_TRUE(stats.predictions_total > 0);
  VT_ASSERT_EQ(stats.decisions_allowed, std::size_t{0});
  VT_ASSERT_EQ(stats.orders_submitted, std::size_t{0});
  VT_ASSERT_EQ(stats.decision_reasons.at("KillSwitch"),
               static_cast<int>(stats.predictions_total));
}

// Regression: realized P&L from closing fills must be fed back into the risk
// engine's daily-loss gate. Before the fix, record_realized_pnl was never
// called during replay, so DailyLossLimit could never trigger.
VT_TEST(Replay, RealizedPnlIsFedToRiskEngine) {
  ReplayConfig config = test_config();
  ReplayEngine engine(config);
  std::istringstream input(to_csv(v_walk()));
  const ReplayStats stats = engine.run(input, "realized-wiring");

  // The scenario must actually close positions and realize P&L, otherwise the
  // assertion below would pass trivially.
  VT_ASSERT_TRUE(stats.realized_pnl != 0.0);
  VT_ASSERT_NEAR(engine.risk().daily_realized_pnl(), stats.realized_pnl, 1e-6);
}

// Regression: once realized losses exceed max_daily_loss, the gate must block
// further orders with the DailyLossLimit reason.
VT_TEST(Replay, DailyLossLimitBlocksOrdersAfterLoss) {
  ReplayConfig config = test_config();
  config.risk.max_daily_loss = 1e-6;  // trip on the first realized loss
  ReplayEngine engine(config);
  std::istringstream input(to_csv(v_walk()));
  const ReplayStats stats = engine.run(input, "daily-loss");

  VT_ASSERT_TRUE(stats.realized_pnl != 0.0);
  VT_ASSERT_TRUE(stats.decision_reasons.count("DailyLossLimit") > 0);
}

VT_TEST(Replay, CostsDoNotInflateNetPnl) {
  ReplayEngine engine(test_config());
  std::istringstream input(to_csv(trending_walk(400)));
  const ReplayStats stats = engine.run(input, "costs");

  VT_ASSERT_TRUE(stats.fills > 0);
  // Gross removes commission and slippage, so net can never beat gross.
  VT_ASSERT_TRUE(stats.net_pnl <= stats.gross_pnl + 1e-9);
  VT_ASSERT_TRUE(stats.commission_total >= 0.0);
  VT_ASSERT_TRUE(stats.slippage_total >= 0.0);
  VT_ASSERT_TRUE(stats.reconciliation.matched);
}

VT_TEST(Replay, SignalCountsMatchRecords) {
  ReplayEngine engine(test_config());
  std::istringstream input(to_csv(trending_walk(400)));
  const ReplayStats stats = engine.run(input, "counts");

  std::size_t signals = 0;
  std::size_t records = 0;
  for (const auto& record : engine.signal_records()) {
    ++records;
    if (record.valid && record.kind != SignalKind::Neutral &&
        record.rejection == RejectionReason::None) {
      ++signals;
    }
  }
  VT_ASSERT_EQ(records, stats.predictions_total);
  VT_ASSERT_EQ(signals, stats.signals);
  VT_ASSERT_EQ(engine.decision_records().size(), stats.predictions_total);
}

VT_TEST(Replay, SampleStreamYieldsContinuationSignals) {
  ReplayEngine engine(test_config());
  std::istringstream input(to_csv(trending_walk(400)));
  const ReplayStats stats = engine.run(input, "signals");
  // The synthetic uptrend must exercise the signal path end-to-end.
  VT_ASSERT_TRUE(stats.signals > 0);
  VT_ASSERT_TRUE(stats.decisions_allowed > 0);
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

VT_TEST(Report, ContainsAllRequiredSections) {
  ReplayEngine engine(test_config());
  std::istringstream input(to_csv(trending_walk(300)));
  engine.run(input, "examples/report_test.csv");

  std::ostringstream report;
  vectra::backtest::write_markdown_report(report, engine, "examples/report_test.csv");
  const std::string text = report.str();

  VT_ASSERT_TRUE(text.find("## 1. Market data") != std::string::npos);
  VT_ASSERT_TRUE(text.find("## 2. Predictions and signal distribution") != std::string::npos);
  VT_ASSERT_TRUE(text.find("## 3. Decisions and risk") != std::string::npos);
  VT_ASSERT_TRUE(text.find("## 4. Execution (simulated)") != std::string::npos);
  VT_ASSERT_TRUE(text.find("## 5. Simulated performance") != std::string::npos);
  VT_ASSERT_TRUE(text.find("## 6. Latency") != std::string::npos);
  VT_ASSERT_TRUE(text.find("## 7. Limitations and assumptions") != std::string::npos);
  VT_ASSERT_TRUE(text.find("not**") != std::string::npos);  // "not calibrated probabilities"
  VT_ASSERT_TRUE(text.find("synthetic") != std::string::npos);
  VT_ASSERT_TRUE(text.find("Run digest") != std::string::npos);
}

VT_TEST(Report, MarksUnavailableLatencyAsNotApplicable) {
  ReplayConfig config = test_config();
  config.risk.kill_switch = true;  // no orders => no order-latency samples
  ReplayEngine engine(config);
  std::istringstream input(to_csv(trending_walk(40)));
  engine.run(input, "no-orders");

  std::ostringstream report;
  vectra::backtest::write_markdown_report(report, engine, "no-orders");
  const std::string text = report.str();
  VT_ASSERT_TRUE(text.find("n/a (only 0 samples") != std::string::npos);
}

VT_TEST(Replay, ForwardSignalStatsAreEvaluatedCausally) {
  ReplayEngine engine(test_config());
  std::istringstream input(to_csv(trending_walk(400)));
  const ReplayStats stats = engine.run(input, "fwd-eval");

  VT_ASSERT_TRUE(stats.signals > 0);
  VT_ASSERT_TRUE(stats.forward_all.evaluated > 0);
  VT_ASSERT_TRUE(stats.forward_all.evaluated <= stats.signals);
  VT_ASSERT_EQ(stats.forward_all.evaluated,
               stats.forward_continuation.evaluated + stats.forward_reversal.evaluated);
  // In a monotonic uptrend, continuation signals have a positive mid move.
  VT_ASSERT_TRUE(stats.forward_continuation.mean_signed_mid_move() > 0.0);
  VT_ASSERT_TRUE(stats.forward_all.non_overlapping > 0);
  VT_ASSERT_TRUE(stats.forward_all.non_overlapping < stats.forward_all.evaluated);
  VT_ASSERT_TRUE(stats.forward_baseline.evaluated >= stats.forward_all.evaluated);
  VT_ASSERT_EQ(stats.calibration_total.count, stats.forward_all.evaluated);
  VT_ASSERT_TRUE(stats.calibration_total.brier_score() >= 0.0);
  VT_ASSERT_TRUE(stats.calibration_total.brier_score() <= 1.0);
}

VT_TEST(Replay, HorizonExitClosesOpenPositionAfterConfiguredEvents) {
  ReplayConfig config = test_config();
  config.hold_horizon_events = 8;
  config.risk.cooldown_events = 50;  // prevent immediate re-entry so exit is isolated
  ReplayEngine engine(config);
  std::istringstream input(to_csv(trending_walk(400)));
  const ReplayStats stats = engine.run(input, "horizon-exit");

  VT_ASSERT_TRUE(stats.horizon_exits_submitted > 0);
  VT_ASSERT_TRUE(stats.realized_pnl != 0.0);
  VT_ASSERT_TRUE(stats.reconciliation.matched);
}

VT_TEST(Replay, CooldownPreventsOrderStackingInReplay) {
  ReplayConfig no_cd = test_config();
  ReplayEngine engine_no_cd(no_cd);
  std::istringstream in1(to_csv(trending_walk(150)));
  const ReplayStats stats_no_cd = engine_no_cd.run(in1, "no-cd");

  ReplayConfig with_cd = test_config();
  with_cd.risk.cooldown_events = 50;
  ReplayEngine engine_cd(with_cd);
  std::istringstream in2(to_csv(trending_walk(150)));
  const ReplayStats stats_cd = engine_cd.run(in2, "with-cd");

  VT_ASSERT_TRUE(stats_cd.orders_submitted < stats_no_cd.orders_submitted);
  VT_ASSERT_TRUE(stats_cd.decision_reasons.count("Cooldown") > 0);
}

VT_TEST(Replay, StopLossAndTakeProfitExitsAreTriggered) {
  // Take-profit on an uptrend.
  ReplayConfig tp_cfg = test_config();
  tp_cfg.take_profit_price = 0.0002;
  tp_cfg.risk.cooldown_events = 100;
  ReplayEngine tp_engine(tp_cfg);
  std::istringstream tp_in(to_csv(trending_walk(300)));
  const ReplayStats tp_stats = tp_engine.run(tp_in, "tp");
  VT_ASSERT_TRUE(tp_stats.take_profit_exits_submitted > 0);

  // Stop-loss on the v_walk reversal.
  ReplayConfig sl_cfg = test_config();
  sl_cfg.stop_loss_price = 0.0003;
  sl_cfg.risk.cooldown_events = 200;
  ReplayEngine sl_engine(sl_cfg);
  std::istringstream sl_in(to_csv(v_walk()));
  const ReplayStats sl_stats = sl_engine.run(sl_in, "sl");
  VT_ASSERT_TRUE(sl_stats.stop_loss_exits_submitted > 0);
}
