// Order model: explicit states, guarded transitions, event stream.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vectra/market/market_event.hpp"

namespace vectra::execution {

enum class Side { Buy = 0, Sell };
enum class OrderType { Market = 0, Limit };

enum class OrderState {
  Created = 0,
  Submitted,
  Acknowledged,
  PartiallyFilled,
  Filled,
  CancelPending,
  Cancelled,
  Rejected,
};

const char* to_string(Side s);
const char* to_string(OrderType t);
const char* to_string(OrderState s);

inline bool is_terminal(OrderState s) {
  return s == OrderState::Filled || s == OrderState::Cancelled || s == OrderState::Rejected;
}

// Transition table. Anything not listed is invalid and must be refused.
bool can_transition(OrderState from, OrderState to);

struct Order {
  std::string order_id;         // venue-assigned id
  std::string client_order_id;  // caller idempotency key
  std::string symbol;
  Side side = Side::Buy;
  OrderType type = OrderType::Market;
  double units = 0.0;
  market::Price limit_price = 0;  // fixed-point, only for Limit orders
  OrderState state = OrderState::Created;

  double filled_units = 0.0;
  double avg_fill_price = 0.0;  // price units (double)
  std::string reject_reason;

  std::int64_t created_ns = 0;
  std::int64_t submitted_ns = 0;
  std::int64_t updated_ns = 0;

  double open_units() const { return units - filled_units; }
  bool is_terminal() const { return vectra::execution::is_terminal(state); }
};

// Applies the transition if the table allows it. Returns false (and leaves
// `order` untouched) when the transition is illegal.
bool apply_transition(Order& order, OrderState to, std::string* error);

struct OrderRequest {
  std::string client_order_id;  // required: duplicate protection key
  std::string symbol;
  Side side = Side::Buy;
  OrderType type = OrderType::Market;
  double units = 0.0;
  market::Price limit_price = 0;
  std::int64_t created_ns = 0;
};

struct Fill {
  std::string order_id;
  std::string client_order_id;
  Side side = Side::Buy;
  double units = 0.0;
  double price = 0.0;          // price units (double)
  double commission = 0.0;     // quote currency
  std::int64_t time_ns = 0;
};

enum class OrderEventType {
  Acknowledged = 0,
  Rejected,
  PartialFill,
  Filled,
  Cancelled,
  Duplicate,   // duplicate client_order_id observed
  CancelRejected,
};

const char* to_string(OrderEventType t);

struct OrderEvent {
  OrderEventType type = OrderEventType::Acknowledged;
  std::string order_id;
  std::string client_order_id;
  Side side = Side::Buy;   // set for fill events
  double units = 0.0;
  double price = 0.0;
  std::int64_t time_ns = 0;
  std::string reason;
};

// Account state derived from fills; used for local-vs-broker reconciliation.
struct AccountSnapshot {
  double position = 0.0;
  double avg_price = 0.0;
  double cash = 0.0;
  double realized_pnl = 0.0;
  double commission_paid = 0.0;
};

}  // namespace vectra::execution
