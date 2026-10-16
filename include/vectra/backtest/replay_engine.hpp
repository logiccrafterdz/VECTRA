// Event-driven replay engine.
//
// Guarantees:
//  * Chronological, single-pass processing: every feature, score and decision
//    at event t is derived only from events <= t (no look-ahead).
//  * Bid/ask-aware execution with configurable commission, slippage and
//    latency; nothing fills at the midpoint by default.
//  * Every prediction, decision, simulated order and fill is recorded.
//
// Explicit non-goals (see docs/data_limitations.md): queue position, book
// depth, market impact, funding/rollover, swaps, spread dynamics beyond the
// observed quotes, and any real order-flow information.
#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "vectra/behavior/behavior_engine.hpp"
#include "vectra/execution/simulated_broker.hpp"
#include "vectra/market/csv_reader.hpp"
#include "vectra/market/validator.hpp"
#include "vectra/prediction/scoring_predictor.hpp"
#include "vectra/risk/risk_engine.hpp"
#include "vectra/telemetry/latency.hpp"

namespace vectra::backtest {

struct ReplayConfig {
  std::string instrument;  // expected symbol, empty = accept every symbol

  market::ValidatorConfig validation;
  behavior::BehaviorEngineConfig behavior;
  prediction::PredictionConfig prediction;
  risk::RiskConfig risk;
  execution::SimBrokerConfig broker;

  double initial_capital = 100000.0;
  // false (default) marks long positions at the bid and short positions at
  // the ask: the conservative choice, never the flattering one.
  bool mark_with_mid = false;

  std::size_t min_latency_samples = 30;
  std::size_t max_equity_curve_points = 1000;
};

struct SignalRecord {
  std::int64_t time_ns = 0;
  std::uint64_t event_index = 0;
  prediction::SignalKind kind = prediction::SignalKind::Neutral;
  prediction::RejectionReason rejection = prediction::RejectionReason::None;
  bool valid = false;
  double exhaustion = 0.0;
  double continuation_score = 0.0;
  double reversal_score = 0.0;
  double confidence = 0.0;
  double expected_move_log = 0.0;
  double direction = 0.0;
  double z_short = 0.0;
  double persistence = 0.0;
  double volatility = 0.0;
  std::string quality_flags;
};

struct DecisionRecord {
  std::int64_t time_ns = 0;
  risk::DecisionAction action = risk::DecisionAction::Reject;
  risk::RejectReason reason = risk::RejectReason::None;
  prediction::SignalKind signal = prediction::SignalKind::Neutral;
  double edge = 0.0;
  double cost = 0.0;
  double expected_move = 0.0;
  double size_units = 0.0;
};

struct ReplayStats {
  // --- input ---
  std::size_t lines_read = 0;
  std::size_t events_parsed = 0;
  std::size_t events_malformed = 0;
  std::size_t events_valid = 0;
  std::size_t events_rejected = 0;
  std::size_t stale_gaps = 0;
  std::map<std::string, int> invalid_reasons;

  // --- prediction ---
  std::size_t predictions_total = 0;
  std::size_t predictions_usable = 0;  // valid + non-neutral signal
  std::size_t signals = 0;
  std::map<std::string, int> signal_kinds;
  std::map<std::string, int> rejection_reasons;

  // --- decisions ---
  std::size_t decisions_allowed = 0;
  std::size_t decisions_rejected = 0;
  std::map<std::string, int> decision_reasons;

  // --- execution ---
  std::size_t orders_submitted = 0;
  std::size_t orders_accepted = 0;
  std::size_t orders_venue_rejected = 0;
  std::size_t orders_cancelled = 0;
  std::size_t orders_duplicate = 0;
  std::size_t order_transitions_invalid = 0;
  std::size_t fills = 0;
  std::size_t partial_fills = 0;

  // --- performance (P&L in quote currency) ---
  double initial_equity = 0.0;
  double final_equity = 0.0;
  double gross_pnl = 0.0;
  double net_pnl = 0.0;
  double commission_total = 0.0;
  double slippage_total = 0.0;
  double spread_cost_total = 0.0;  // informational: half-spread per fill
  double realized_pnl = 0.0;
  double max_drawdown = 0.0;
  double max_drawdown_pct = 0.0;
  double peak_equity = 0.0;

  std::int64_t first_event_time_ns = 0;
  std::int64_t last_event_time_ns = 0;

  // --- latency: event/signal/decision are steady_clock durations; the order
  //     row is venue model time (submit -> ack), quantized by the tick interval
  telemetry::QuantileStats event_latency;
  telemetry::QuantileStats signal_latency;
  telemetry::QuantileStats decision_latency;
  telemetry::QuantileStats order_latency;  // simulated submit -> ack (venue clock)

  execution::ReconciliationReport reconciliation;
  std::uint64_t digest = 0;  // deterministic fingerprint of the run
};

class ReplayEngine {
 public:
  explicit ReplayEngine(ReplayConfig config);

  // Reads CSV ticks and runs the full pipeline. Returns aggregate statistics.
  ReplayStats run(std::istream& input, const std::string& source_name);

  const ReplayStats& stats() const { return stats_; }
  const ReplayConfig& config() const { return config_; }

  const std::vector<SignalRecord>& signal_records() const { return signal_records_; }
  const std::vector<DecisionRecord>& decision_records() const { return decision_records_; }
  const std::vector<execution::OrderEvent>& order_events() const { return order_events_; }
  const std::vector<execution::Fill>& fills() const { return broker_.fills(); }
  const std::vector<std::pair<std::int64_t, double>>& equity_curve() const {
    return equity_curve_;
  }

  // Read-only view of the risk engine state (used to verify that realized P&L
  // is actually fed back into the daily-loss gate).
  const risk::RiskEngine& risk() const { return risk_; }

 private:
  void handle_order_events(std::int64_t now_ns, market::Price bid, market::Price ask);
  void apply_local_fill(const execution::OrderEvent& event, double half_spread_price);
  void update_equity(market::Price bid, market::Price ask, std::int64_t now_ns);
  void finalize();

  ReplayConfig config_;
  ReplayStats stats_;

  market::EventValidator validator_;
  behavior::BehaviorEngine behavior_;
  prediction::ScoringPredictor predictor_;
  risk::RiskEngine risk_;
  execution::SimulatedBroker broker_;

  execution::AccountSnapshot local_account_;
  execution::AccountSnapshot gross_account_;
  double commission_paid_ = 0.0;
  double slippage_paid_ = 0.0;

  std::map<std::string, std::int64_t> submit_times_;
  std::int64_t stream_time_ns_ = 0;
  bool has_stream_time_ = false;
  std::uint64_t request_counter_ = 0;

  double equity_peak_ = 0.0;
  std::size_t equity_samples_ = 0;

  std::vector<SignalRecord> signal_records_;
  std::vector<DecisionRecord> decision_records_;
  std::vector<execution::OrderEvent> order_events_;
  std::vector<std::pair<std::int64_t, double>> equity_curve_;

  telemetry::LatencyRecorder event_latency_;
  telemetry::LatencyRecorder signal_latency_;
  telemetry::LatencyRecorder decision_latency_;
  telemetry::LatencyRecorder order_latency_;

  std::uint64_t digest_ = 14695981039346656037ULL;  // FNV-1a offset basis
  bool finalized_ = false;
};

}  // namespace vectra::backtest
