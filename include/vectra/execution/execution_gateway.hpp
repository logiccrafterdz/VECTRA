// Abstract execution interface.
//
// The initial version only ships the SimulatedBroker, but the interface is
// deliberately broker-agnostic so that a properly authorized live-broker
// adapter can be added later without touching the risk or backtest layers.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vectra/execution/order.hpp"

namespace vectra::execution {

class IExecutionGateway {
 public:
  virtual ~IExecutionGateway() = default;

  // Submits an order. Returns the accepted order, or std::nullopt when the
  // request was refused. A duplicate client_order_id is refused by default
  // (see `duplicate()`), never silently double-submitted.
  virtual std::optional<Order> submit(const OrderRequest& request, std::int64_t now_ns) = 0;

  // Requests cancellation. Returns false when the order is already terminal
  // or already cancelling.
  virtual bool cancel(const std::string& order_id, std::int64_t now_ns) = 0;

  // Feeds a market event into the gateway: it drives simulated time, so
  // acknowledgements, rejections, fills and cancellations become due.
  virtual void on_market(const market::MarketEvent& event) = 0;

  // Returns and clears buffered order events.
  virtual std::vector<OrderEvent> drain_events() = 0;

  virtual const std::vector<Order>& orders() const = 0;
  virtual const std::vector<Fill>& fills() const = 0;
  virtual std::optional<Order> find(const std::string& order_id) const = 0;

  // Venue-side account view (for reconciliation against local bookkeeping).
  virtual const AccountSnapshot& account() const = 0;

  // Number of duplicate submissions observed since construction.
  virtual std::size_t duplicate_submissions() const = 0;
};

struct ReconciliationReport {
  bool matched = false;
  double position_diff = 0.0;
  double cash_diff = 0.0;
  double realized_diff = 0.0;
  std::string detail;
};

// Compares an independently maintained local account against the gateway's
// account. Tolerances absorb floating-point noise, not real discrepancies.
ReconciliationReport reconcile(const AccountSnapshot& local, const AccountSnapshot& broker,
                               double tolerance = 1e-6);

}  // namespace vectra::execution
