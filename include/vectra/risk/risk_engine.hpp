// Decision and risk engine.
//
// The risk engine sits BETWEEN prediction and execution: a prediction never
// becomes an order without passing every configured limit. It has no idea how
// to trade; it only decides Allow / Reject and at what size.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "vectra/market/market_event.hpp"
#include "vectra/prediction/prediction.hpp"

namespace vectra::risk {

struct RiskConfig {
  bool enabled = true;
  bool kill_switch = false;  // when true, every decision is rejected

  // --- Edge and cost model (absolute price units, i.e. price differences) ---
  double min_expected_edge = 0.00015;  // UNCALIBRATED: required edge after cost
  double commission = 0.00001;         // UNCALIBRATED: per-side cost, price units
  double slippage = 0.00002;           // UNCALIBRATED: per-side cost, price units
  double max_spread = 0.00040;         // reject quotes wider than this
  bool round_trip_cost = false;        // when true, edge gate deducts entry + exit cost

  // --- Position and exposure limits ---
  double order_units = 5000.0;    // base order size when limits allow
  double max_position_units = 50000.0;  // absolute base-unit position
  double max_exposure = 60000.0;  // |position| * mid (quote notional)

  // --- Loss limits ---
  // Daily realized loss beyond this blocks all new risk. Negative => disabled.
  double max_daily_loss = 250.0;

  // --- Order limits ---
  int max_open_orders = 3;
  int max_orders_per_day = 40;
  std::size_t cooldown_events = 0;  // min valid events between order submissions (0 = off)
  std::int64_t cooldown_ns = 0;     // min provider-time ns between order submissions (0 = off)

  // --- Data health gate ---
  std::size_t data_health_window = 200;   // rolling window of validity flags
  double max_invalid_ratio = 0.20;        // above this, data is "unreliable"
  std::int64_t data_stale_gate_ns = 2'000'000'000;  // no valid event for 2 s
};

enum class DecisionAction { Allow = 0, Reject };

enum class RejectReason {
  None = 0,
  KillSwitch,
  RiskDisabled,
  InvalidSignal,     // prediction failed a data gate
  NoSignal,          // Neutral / no rule matched
  DataUnreliable,    // invalid-ratio or staleness gate tripped
  CostTooHigh,       // spread above the configured ceiling
  InsufficientEdge,  // expected move minus cost below threshold
  MaxPosition,
  MaxExposure,
  DailyLossLimit,
  MaxOpenOrders,
  MaxOrdersPerDay,
  Cooldown,
};

const char* to_string(RejectReason r);
const char* to_string(DecisionAction a);

struct Decision {
  DecisionAction action = DecisionAction::Reject;
  RejectReason reason = RejectReason::None;
  std::string detail;

  prediction::SignalKind signal = prediction::SignalKind::Neutral;
  std::int64_t time_ns = 0;

  double cost = 0.0;           // estimated per-side cost, price units
  double expected_move = 0.0;  // |expected| price move, price units
  double edge = 0.0;           // expected_move - cost
  double size_units = 0.0;     // base units when allowed

  bool allowed() const { return action == DecisionAction::Allow; }
};

// Single-threaded by design. All state is plain data, mutated explicitly by
// the replay loop through the record_* methods.
class RiskEngine {
 public:
  explicit RiskEngine(RiskConfig config);

  // Evaluates a prediction against the current quote and the live limits.
  Decision evaluate(const prediction::Prediction& prediction,
                    const market::MarketEvent& event, std::int64_t now_ns);

  // --- state updates driven by the replay loop ---
  void record_data_point(bool valid_event, std::int64_t now_ns);
  void record_position(double units);
  void record_open_orders(int count);
  void record_order_placed();
  void record_realized_pnl(double pnl_delta);
  void on_new_day(std::int64_t day_index);

  void set_kill_switch(bool enabled);
  bool kill_switch() const { return config_.kill_switch; }

  double position_units() const { return position_units_; }
  int open_orders() const { return open_orders_; }
  int orders_today() const { return orders_today_; }
  double daily_realized_pnl() const { return daily_pnl_; }
  bool data_reliable() const;

  const RiskConfig& config() const { return config_; }

 private:
  RiskConfig config_;
  double position_units_ = 0.0;
  int open_orders_ = 0;
  int orders_today_ = 0;
  double daily_pnl_ = 0.0;
  std::int64_t day_index_ = 0;

  std::deque<bool> data_window_;
  std::size_t data_window_valid_ = 0;
  bool has_valid_event_ = false;
  std::int64_t last_valid_event_ns_ = 0;
  bool has_placed_order_ = false;
  std::size_t valid_events_since_last_order_ = 0;
  std::int64_t last_order_time_ns_ = 0;
};

}  // namespace vectra::risk
