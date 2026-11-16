// Decision and risk engine implementation.
#include "vectra/risk/risk_engine.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vectra::risk {

const char* to_string(DecisionAction a) {
  switch (a) {
    case DecisionAction::Allow:  return "Allow";
    case DecisionAction::Reject: return "Reject";
  }
  return "Unknown";
}

const char* to_string(RejectReason r) {
  switch (r) {
    case RejectReason::None:               return "None";
    case RejectReason::KillSwitch:         return "KillSwitch";
    case RejectReason::RiskDisabled:       return "RiskDisabled";
    case RejectReason::InvalidSignal:      return "InvalidSignal";
    case RejectReason::NoSignal:           return "NoSignal";
    case RejectReason::DataUnreliable:     return "DataUnreliable";
    case RejectReason::CostTooHigh:        return "CostTooHigh";
    case RejectReason::InsufficientEdge:   return "InsufficientEdge";
    case RejectReason::MaxPosition:        return "MaxPosition";
    case RejectReason::MaxExposure:        return "MaxExposure";
    case RejectReason::DailyLossLimit:     return "DailyLossLimit";
    case RejectReason::MaxOpenOrders:      return "MaxOpenOrders";
    case RejectReason::MaxOrdersPerDay:    return "MaxOrdersPerDay";
    case RejectReason::Cooldown:           return "Cooldown";
    case RejectReason::SessionBlackout:    return "SessionBlackout";
  }
  return "Unknown";
}

namespace {

Decision reject(RejectReason reason, prediction::SignalKind kind, std::int64_t now_ns,
                std::string detail) {
  Decision d;
  d.action = DecisionAction::Reject;
  d.reason = reason;
  d.signal = kind;
  d.time_ns = now_ns;
  d.detail = std::move(detail);
  return d;
}

}  // namespace

RiskEngine::RiskEngine(RiskConfig config) : config_(std::move(config)) {}

void RiskEngine::set_kill_switch(bool enabled) { config_.kill_switch = enabled; }

bool RiskEngine::data_reliable() const {
  if (config_.data_health_window == 0 || data_window_.empty()) {
    return true;
  }
  const double ratio =
      1.0 - static_cast<double>(data_window_valid_) / static_cast<double>(data_window_.size());
  return ratio <= config_.max_invalid_ratio;
}

void RiskEngine::record_data_point(bool valid_event, std::int64_t now_ns) {
  if (config_.data_health_window > 0) {
    if (data_window_.size() >= config_.data_health_window) {
      if (data_window_.front()) {
        --data_window_valid_;
      }
      data_window_.pop_front();
    }
    data_window_.push_back(valid_event);
    if (valid_event) {
      ++data_window_valid_;
    }
  }
  if (valid_event) {
    has_valid_event_ = true;
    last_valid_event_ns_ = now_ns;
    if (has_placed_order_) {
      ++valid_events_since_last_order_;
    }
  }
}

void RiskEngine::record_position(double units) { position_units_ = units; }
void RiskEngine::record_open_orders(int count) { open_orders_ = count; }
void RiskEngine::record_order_placed() {
  ++orders_today_;
  has_placed_order_ = true;
  valid_events_since_last_order_ = 0;
  last_order_time_ns_ = last_valid_event_ns_;
}

void RiskEngine::record_realized_pnl(double pnl_delta) { daily_pnl_ += pnl_delta; }

void RiskEngine::on_new_day(std::int64_t day_index) {
  if (day_index != day_index_) {
    day_index_ = day_index;
    daily_pnl_ = 0.0;
    orders_today_ = 0;
  }
}

Decision RiskEngine::evaluate(const prediction::Prediction& p,
                              const market::MarketEvent& event, std::int64_t now_ns) {
  const prediction::SignalKind kind = p.kind;

  if (!config_.enabled) {
    return reject(RejectReason::RiskDisabled, kind, now_ns, "risk engine disabled by config");
  }
  if (config_.kill_switch) {
    return reject(RejectReason::KillSwitch, kind, now_ns, "kill switch active");
  }
  if (config_.max_daily_loss >= 0.0 && daily_pnl_ <= -config_.max_daily_loss) {
    return reject(RejectReason::DailyLossLimit, kind, now_ns,
                  "daily realized P&L " + std::to_string(daily_pnl_) + " hit limit " +
                      std::to_string(-config_.max_daily_loss));
  }

  if (!p.valid) {
    return reject(RejectReason::InvalidSignal, kind, now_ns,
                  std::string("prediction rejected: ") + prediction::to_string(p.rejection));
  }
  if (kind == prediction::SignalKind::Neutral ||
      p.rejection != prediction::RejectionReason::None) {
    return reject(RejectReason::NoSignal, kind, now_ns,
                  std::string("no tradable signal: ") + prediction::to_string(p.rejection));
  }

  // Data health: unreliable feed => no new risk, regardless of signal quality.
  const bool stale_gate_tripped =
      has_valid_event_ && config_.data_stale_gate_ns > 0 &&
      (now_ns - last_valid_event_ns_) > config_.data_stale_gate_ns;
  if (stale_gate_tripped) {
    return reject(RejectReason::DataUnreliable, kind, now_ns,
                  "no valid event within the stale gate");
  }
  if (!data_reliable()) {
    return reject(RejectReason::DataUnreliable, kind, now_ns,
                  "invalid-event ratio above the configured limit");
  }

  const double mid = event.mid_price();
  if (mid <= 0.0) {
    return reject(RejectReason::InvalidSignal, kind, now_ns, "non-positive midpoint");
  }

  const double spread_price = market::price_to_double(event.spread());
  if (spread_price > config_.max_spread) {
    return reject(RejectReason::CostTooHigh, kind, now_ns,
                  "spread " + std::to_string(spread_price) + " > " +
                      std::to_string(config_.max_spread));
  }

  // Cost model: half the spread actually crossed + commission + slippage per
  // side, or full round-trip when round_trip_cost is enabled.
  const double one_way_cost = spread_price * 0.5 + config_.commission + config_.slippage;
  const double cost = config_.round_trip_cost ? (2.0 * one_way_cost) : one_way_cost;
  // Convert the log-return expectation into a price move (expm1 is exact for
  // the log return; the linear approximation would drift on larger moves).
  const double expected_move = std::expm1(std::fabs(p.expected_move_log)) * mid;
  const double edge = expected_move - cost;

  Decision d;
  d.signal = kind;
  d.time_ns = now_ns;
  d.cost = cost;
  d.expected_move = expected_move;
  d.edge = edge;

  if (edge < config_.min_expected_edge) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::InsufficientEdge;
    d.detail = "edge " + std::to_string(edge) + " < required " +
               std::to_string(config_.min_expected_edge);
    return d;
  }

  if (config_.max_open_orders >= 0 && open_orders_ >= config_.max_open_orders) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::MaxOpenOrders;
    d.detail = "open orders " + std::to_string(open_orders_) + " at limit";
    return d;
  }
  if (config_.max_orders_per_day >= 0 && orders_today_ >= config_.max_orders_per_day) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::MaxOrdersPerDay;
    d.detail = "orders today " + std::to_string(orders_today_) + " at limit";
    return d;
  }
  if (config_.cooldown_events > 0 && has_placed_order_ &&
      valid_events_since_last_order_ < config_.cooldown_events) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::Cooldown;
    d.detail = "cooldown active (" + std::to_string(valid_events_since_last_order_) + "/" +
               std::to_string(config_.cooldown_events) + " events)";
    return d;
  }
  if (config_.cooldown_ns > 0 && has_placed_order_ &&
      (now_ns - last_order_time_ns_) < config_.cooldown_ns) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::Cooldown;
    d.detail = "time cooldown active";
    return d;
  }

  const double direction = p.direction >= 0.0 ? 1.0 : -1.0;

  // Position headroom in the direction of the new order.
  double allowed = config_.max_position_units;
  if (direction > 0.0) {
    allowed -= position_units_;
  } else {
    allowed += position_units_;
  }
  if (allowed <= 0.0) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::MaxPosition;
    d.detail = "position " + std::to_string(position_units_) + " at limit";
    return d;
  }

  // Exposure headroom (|position| * mid in quote notional).
  double exposure_allowed = allowed;
  const double max_position_by_exposure = config_.max_exposure / mid;
  if (direction > 0.0) {
    exposure_allowed = std::min(exposure_allowed, max_position_by_exposure - position_units_);
  } else {
    exposure_allowed = std::min(exposure_allowed, position_units_ + max_position_by_exposure);
  }
  if (exposure_allowed <= 0.0) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::MaxExposure;
    d.detail = "exposure limit blocks new risk at mid " + std::to_string(mid);
    return d;
  }

  const double size = std::min(config_.order_units, std::min(allowed, exposure_allowed));
  if (size <= 0.0) {
    d.action = DecisionAction::Reject;
    d.reason = RejectReason::MaxPosition;
    d.detail = "computed size is zero";
    return d;
  }

  d.action = DecisionAction::Allow;
  d.reason = RejectReason::None;
  d.size_units = size;
  d.detail = "edge " + std::to_string(edge) + " after cost " + std::to_string(cost);
  return d;
}

}  // namespace vectra::risk
