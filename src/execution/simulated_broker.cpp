// Simulated execution gateway implementation.
#include "vectra/execution/simulated_broker.hpp"

#include <algorithm>
#include <cmath>

namespace vectra::execution {

ReconciliationReport reconcile(const AccountSnapshot& local, const AccountSnapshot& broker,
                               double tolerance) {
  ReconciliationReport report;
  report.position_diff = local.position - broker.position;
  report.cash_diff = local.cash - broker.cash;
  report.realized_diff = local.realized_pnl - broker.realized_pnl;

  const bool position_ok = std::fabs(report.position_diff) <= tolerance;
  const bool cash_ok = std::fabs(report.cash_diff) <= tolerance;
  const bool realized_ok = std::fabs(report.realized_diff) <= tolerance;
  report.matched = position_ok && cash_ok && realized_ok;

  if (report.matched) {
    report.detail = "local and broker accounts agree";
  } else {
    report.detail = "mismatch";
    if (!position_ok) {
      report.detail += " position_diff=" + std::to_string(report.position_diff);
    }
    if (!cash_ok) {
      report.detail += " cash_diff=" + std::to_string(report.cash_diff);
    }
    if (!realized_ok) {
      report.detail += " realized_diff=" + std::to_string(report.realized_diff);
    }
  }
  return report;
}

SimulatedBroker::SimulatedBroker(SimBrokerConfig config) : config_(std::move(config)) {
  if (config_.ack_latency_ns < 0) {
    config_.ack_latency_ns = 0;
  }
  if (config_.fill_latency_ns < 0) {
    config_.fill_latency_ns = 0;
  }
  if (config_.cancel_latency_ns < 0) {
    config_.cancel_latency_ns = 0;
  }
  if (config_.partial_fill_fraction <= 0.0 || config_.partial_fill_fraction > 1.0) {
    config_.partial_fill_fraction = 1.0;
  }
  rng_state_ = config_.seed;
}

double SimulatedBroker::random_unit() {
  // splitmix64
  rng_state_ += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = rng_state_;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z = z ^ (z >> 31);
  const double unit = static_cast<double>(z >> 11) / 9007199254740992.0;  // 2^53
  return unit;
}

void SimulatedBroker::emit(OrderEvent event) { events_.push_back(std::move(event)); }

Order* SimulatedBroker::find_mutable(const std::string& order_id) {
  for (auto& order : orders_) {
    if (order.order_id == order_id) {
      return &order;
    }
  }
  return nullptr;
}

std::optional<Order> SimulatedBroker::find(const std::string& order_id) const {
  for (const auto& order : orders_) {
    if (order.order_id == order_id) {
      return order;
    }
  }
  return std::nullopt;
}

void SimulatedBroker::schedule(ActionKind kind, const std::string& order_id,
                               std::int64_t due_ns) {
  PendingAction action;
  action.kind = kind;
  action.order_id = order_id;
  action.due_ns = due_ns;
  action.sequence = next_action_sequence_++;
  pending_.push_back(action);
}

std::optional<Order> SimulatedBroker::submit(const OrderRequest& request,
                                              std::int64_t now_ns) {
  // Request validation: refuse garbage before it reaches the venue model.
  if (request.client_order_id.empty() || request.symbol.empty() || request.units <= 0.0) {
    OrderEvent event;
    event.type = OrderEventType::Rejected;
    event.client_order_id = request.client_order_id;
    event.time_ns = now_ns;
    event.reason = "invalid request: empty id/symbol or non-positive units";
    emit(event);
    return std::nullopt;
  }

  // Duplicate protection: the same client id never becomes a second order.
  const auto existing = client_to_order_.find(request.client_order_id);
  if (existing != client_to_order_.end()) {
    ++duplicates_;
    OrderEvent event;
    event.type = OrderEventType::Duplicate;
    event.order_id = existing->second;
    event.client_order_id = request.client_order_id;
    event.time_ns = now_ns;
    event.reason = "client_order_id already submitted";
    emit(event);
    auto order = find(existing->second);
    return order;  // idempotent: hand back the original order
  }

  Order order;
  order.order_id = "SIM-" + std::to_string(next_order_number_++);
  order.client_order_id = request.client_order_id;
  order.symbol = request.symbol;
  order.side = request.side;
  order.type = request.type;
  order.units = request.units;
  order.limit_price = request.limit_price;
  order.state = OrderState::Created;
  order.created_ns = now_ns;
  order.submitted_ns = now_ns;
  order.updated_ns = now_ns;

  std::string error;
  if (!apply_transition(order, OrderState::Submitted, &error)) {
    return std::nullopt;
  }
  orders_.push_back(order);
  const std::string order_id = order.order_id;
  client_to_order_[request.client_order_id] = order_id;

  // Venue decision: reject with configured probability (deterministic PRNG).
  const bool venue_reject =
      config_.reject_probability > 0.0 && random_unit() < config_.reject_probability;

  if (venue_reject) {
    schedule(ActionKind::Reject, order_id, now_ns + config_.ack_latency_ns);
  } else {
    // A fill can only happen after acknowledgement, so the fill action must
    // never be due before the ack action even if the configured fill latency
    // is shorter than the ack latency; otherwise the fill would be attempted
    // while the order is still Submitted and silently dropped.
    const std::int64_t fill_due =
        now_ns + std::max(config_.fill_latency_ns, config_.ack_latency_ns);
    schedule(ActionKind::Ack, order_id, now_ns + config_.ack_latency_ns);
    schedule(ActionKind::FillAttempt, order_id, fill_due);
  }
  return find(order_id);
}

bool SimulatedBroker::cancel(const std::string& order_id, std::int64_t now_ns) {
  Order* order = find_mutable(order_id);
  if (order == nullptr) {
    return false;
  }
  if (order->state == OrderState::CancelPending) {
    return false;  // already cancelling
  }
  if (is_terminal(order->state)) {
    return false;
  }
  std::string error;
  if (!apply_transition(*order, OrderState::CancelPending, &error)) {
    OrderEvent event;
    event.type = OrderEventType::CancelRejected;
    event.order_id = order_id;
    event.client_order_id = order->client_order_id;
    event.time_ns = now_ns;
    event.reason = error;
    emit(event);
    return false;
  }
  order->updated_ns = now_ns;
  schedule(ActionKind::Cancel, order_id, now_ns + config_.cancel_latency_ns);
  return true;
}

void SimulatedBroker::apply_fill(Order& order, double units, double price,
                                 std::int64_t now_ns) {
  units = std::min(units, order.open_units());
  if (units <= 0.0) {
    return;
  }

  Fill fill;
  fill.order_id = order.order_id;
  fill.client_order_id = order.client_order_id;
  fill.side = order.side;
  fill.units = units;
  fill.price = price;
  fill.time_ns = now_ns;
  fill.commission = units * config_.commission_per_unit;
  fills_.push_back(fill);

  const double filled_before = order.filled_units;
  order.filled_units += units;
  order.avg_fill_price =
      (filled_before * order.avg_fill_price + units * price) / order.filled_units;
  order.updated_ns = now_ns;

  // --- account update (average-price + realized P&L) ---
  const double signed_units = order.side == Side::Buy ? units : -units;
  if (account_.position == 0.0 || account_.position * signed_units > 0.0) {
    const double total = account_.position + signed_units;
    const double weight_old = std::fabs(account_.position);
    const double weight_new = units;
    account_.avg_price = (weight_old * account_.avg_price + weight_new * price) /
                         (weight_old + weight_new);
    account_.position = total;
  } else {
    const double closing = std::min(units, std::fabs(account_.position));
    const double per_unit = account_.position > 0.0 ? (price - account_.avg_price)
                                                    : (account_.avg_price - price);
    account_.realized_pnl += per_unit * closing;
    const double new_position = account_.position + signed_units;
    if (new_position == 0.0) {
      account_.position = 0.0;
      account_.avg_price = 0.0;
    } else if ((new_position > 0.0) == (account_.position > 0.0)) {
      account_.position = new_position;  // reduced: average price unchanged
    } else {
      account_.position = new_position;  // flipped through zero
      account_.avg_price = price;
    }
  }
  account_.cash -= signed_units * price;
  account_.commission_paid += fill.commission;
  account_.cash -= fill.commission;

  const bool fully_filled = order.open_units() <= 1e-12;
  if (fully_filled) {
    std::string error;
    // Guards above guarantee Acknowledged/PartiallyFilled/CancelPending here.
    apply_transition(order, OrderState::Filled, &error);
    OrderEvent event;
    event.type = OrderEventType::Filled;
    event.order_id = order.order_id;
    event.client_order_id = order.client_order_id;
    event.side = order.side;
    event.units = units;
    event.price = price;
    event.time_ns = now_ns;
    emit(event);
  } else {
    const bool was_partial = order.state == OrderState::PartiallyFilled;
    if (!was_partial) {
      std::string error;
      if (!apply_transition(order, OrderState::PartiallyFilled, &error)) {
        return;
      }
    }
    OrderEvent event;
    event.type = OrderEventType::PartialFill;
    event.order_id = order.order_id;
    event.client_order_id = order.client_order_id;
    event.side = order.side;
    event.units = units;
    event.price = price;
    event.time_ns = now_ns;
    emit(event);
    schedule(ActionKind::FillAttempt, order.order_id, now_ns + config_.fill_latency_ns);
  }
}

void SimulatedBroker::process_action(const PendingAction& action, std::int64_t now_ns,
                                     market::Price bid, market::Price ask) {
  Order* order = find_mutable(action.order_id);
  if (order == nullptr) {
    return;
  }

  switch (action.kind) {
    case ActionKind::Ack: {
      if (order->state != OrderState::Submitted) {
        return;
      }
      std::string error;
      if (apply_transition(*order, OrderState::Acknowledged, &error)) {
        order->updated_ns = now_ns;
        OrderEvent event;
        event.type = OrderEventType::Acknowledged;
        event.order_id = order->order_id;
        event.client_order_id = order->client_order_id;
        event.time_ns = now_ns;
        emit(event);
      }
      break;
    }
    case ActionKind::Reject: {
      if (is_terminal(order->state)) {
        return;
      }
      std::string error;
      if (apply_transition(*order, OrderState::Rejected, &error)) {
        order->reject_reason = "venue rejection (simulated)";
        order->updated_ns = now_ns;
        OrderEvent event;
        event.type = OrderEventType::Rejected;
        event.order_id = order->order_id;
        event.client_order_id = order->client_order_id;
        event.time_ns = now_ns;
        event.reason = order->reject_reason;
        emit(event);
      }
      break;
    }
    case ActionKind::Cancel: {
      if (order->state != OrderState::CancelPending) {
        return;  // filled or otherwise changed state in the meantime
      }
      std::string error;
      if (apply_transition(*order, OrderState::Cancelled, &error)) {
        order->updated_ns = now_ns;
        OrderEvent event;
        event.type = OrderEventType::Cancelled;
        event.order_id = order->order_id;
        event.client_order_id = order->client_order_id;
        event.time_ns = now_ns;
        emit(event);
      }
      break;
    }
    case ActionKind::FillAttempt: {
      if (is_terminal(order->state) || order->state == OrderState::CancelPending) {
        return;
      }
      if (order->state == OrderState::Submitted) {
        return;  // fills only happen after acknowledgement
      }
      if (!has_quote_ || bid <= 0 || ask <= 0) {
        return;  // no usable quote yet: retry on the next market event
      }

      double units = order->open_units();
      const bool partial =
          config_.partial_fill_probability > 0.0 && units > 1.0 &&
          random_unit() < config_.partial_fill_probability;
      if (partial) {
        units = std::max(1.0, units * config_.partial_fill_fraction);
        units = std::min(units, order->open_units());
      }

      // Bid/ask-aware execution: buys lift the offer, sells hit the bid.
      double price = 0.0;
      if (order->side == Side::Buy) {
        price = market::price_to_double(ask) + config_.slippage;
        if (order->type == OrderType::Limit && order->limit_price > 0 &&
            market::price_to_double(ask) > market::price_to_double(order->limit_price)) {
          // Limit not marketable: keep working, retry later.
          schedule(ActionKind::FillAttempt, order->order_id, now_ns + config_.fill_latency_ns);
          return;
        }
      } else {
        price = market::price_to_double(bid) - config_.slippage;
        if (order->type == OrderType::Limit && order->limit_price > 0 &&
            market::price_to_double(bid) < market::price_to_double(order->limit_price)) {
          schedule(ActionKind::FillAttempt, order->order_id, now_ns + config_.fill_latency_ns);
          return;
        }
      }
      apply_fill(*order, units, price, now_ns);
      break;
    }
  }
}

void SimulatedBroker::on_market(const market::MarketEvent& event) {
  if (event.validity != market::Validity::Ok) {
    return;  // invalid quotes never drive fills
  }
  bid_ = event.bid;
  ask_ = event.ask;
  has_quote_ = true;

  // Process due actions in (due, sequence) order. Actions scheduled while
  // processing are appended and may also be due now; loop until stable, but
  // never reprocess an action twice.
  bool progressed = true;
  while (progressed) {
    progressed = false;
    std::stable_sort(pending_.begin(), pending_.end(),
                     [](const PendingAction& a, const PendingAction& b) {
                       if (a.due_ns != b.due_ns) {
                         return a.due_ns < b.due_ns;
                       }
                       return a.sequence < b.sequence;
                     });
    for (std::size_t i = 0; i < pending_.size(); ++i) {
      if (pending_[i].due_ns > event.source_time_ns) {
        continue;
      }
      PendingAction action = pending_[i];
      pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
      process_action(action, event.source_time_ns, bid_, ask_);
      progressed = true;
      break;  // restart scan: process_action may have appended new actions
    }
  }
}

std::vector<OrderEvent> SimulatedBroker::drain_events() {
  std::vector<OrderEvent> out;
  out.swap(events_);
  return out;
}

}  // namespace vectra::execution
