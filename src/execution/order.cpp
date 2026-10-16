// Order model: state transition table and enum names.
#include "vectra/execution/order.hpp"

namespace vectra::execution {

const char* to_string(Side s) { return s == Side::Buy ? "Buy" : "Sell"; }

const char* to_string(OrderType t) { return t == OrderType::Market ? "Market" : "Limit"; }

const char* to_string(OrderState s) {
  switch (s) {
    case OrderState::Created:         return "Created";
    case OrderState::Submitted:       return "Submitted";
    case OrderState::Acknowledged:    return "Acknowledged";
    case OrderState::PartiallyFilled: return "PartiallyFilled";
    case OrderState::Filled:          return "Filled";
    case OrderState::CancelPending:   return "CancelPending";
    case OrderState::Cancelled:       return "Cancelled";
    case OrderState::Rejected:        return "Rejected";
  }
  return "Unknown";
}

const char* to_string(OrderEventType t) {
  switch (t) {
    case OrderEventType::Acknowledged:   return "Acknowledged";
    case OrderEventType::Rejected:       return "Rejected";
    case OrderEventType::PartialFill:    return "PartialFill";
    case OrderEventType::Filled:         return "Filled";
    case OrderEventType::Cancelled:      return "Cancelled";
    case OrderEventType::Duplicate:      return "Duplicate";
    case OrderEventType::CancelRejected: return "CancelRejected";
  }
  return "Unknown";
}

bool can_transition(OrderState from, OrderState to) {
  if (from == to) {
    // Idempotent self-transition is only meaningful while an order is still
    // working (repeated partial fills).
    return from == OrderState::PartiallyFilled;
  }
  switch (from) {
    case OrderState::Created:
      return to == OrderState::Submitted || to == OrderState::Rejected ||
             to == OrderState::CancelPending;
    case OrderState::Submitted:
      return to == OrderState::Acknowledged || to == OrderState::Rejected ||
             to == OrderState::CancelPending || to == OrderState::Filled;
    case OrderState::Acknowledged:
      return to == OrderState::PartiallyFilled || to == OrderState::Filled ||
             to == OrderState::CancelPending;
    case OrderState::PartiallyFilled:
      return to == OrderState::Filled || to == OrderState::CancelPending ||
             to == OrderState::Cancelled;
    case OrderState::CancelPending:
      return to == OrderState::Cancelled || to == OrderState::Filled ||
             to == OrderState::PartiallyFilled;
    case OrderState::Filled:
    case OrderState::Cancelled:
    case OrderState::Rejected:
      return false;  // terminal
  }
  return false;
}

bool apply_transition(Order& order, OrderState to, std::string* error) {
  if (!can_transition(order.state, to)) {
    if (error != nullptr) {
      *error = std::string("illegal transition ") + to_string(order.state) + " -> " +
               to_string(to);
    }
    return false;
  }
  order.state = to;
  return true;
}

}  // namespace vectra::execution
