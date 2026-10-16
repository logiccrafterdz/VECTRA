// Simulated execution gateway: deterministic, latency-aware, bid/ask-aware.
//
// What it models: submission latency, venue acknowledgement/rejection,
// partial fills, cancellations, bid/ask crossing plus configurable slippage
// and commission.
// What it does NOT model: queue position, real book depth, market impact,
// rebates, partial-fill selection bias, or any counterparty behaviour.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vectra/execution/execution_gateway.hpp"

namespace vectra::execution {

struct SimBrokerConfig {
  std::int64_t ack_latency_ns = 50'000'000;     // 50 ms
  std::int64_t fill_latency_ns = 100'000'000;   // 100 ms
  std::int64_t cancel_latency_ns = 100'000'000; // 100 ms

  double reject_probability = 0.05;       // UNCALIBRATED venue rejection model
  double partial_fill_probability = 0.30; // UNCALIBRATED
  double partial_fill_fraction = 0.5;     // UNCALIBRATED fraction of remaining
  double slippage = 0.00001;              // price units against the trader
  double commission_per_unit = 0.0;       // quote currency per base unit

  bool allow_same_event_fill = false;  // must stay false (no look-ahead)
  std::uint64_t seed = 42;
};

class SimulatedBroker final : public IExecutionGateway {
 public:
  explicit SimulatedBroker(SimBrokerConfig config);

  std::optional<Order> submit(const OrderRequest& request, std::int64_t now_ns) override;
  bool cancel(const std::string& order_id, std::int64_t now_ns) override;
  void on_market(const market::MarketEvent& event) override;
  std::vector<OrderEvent> drain_events() override;

  const std::vector<Order>& orders() const override { return orders_; }
  const std::vector<Fill>& fills() const override { return fills_; }
  std::optional<Order> find(const std::string& order_id) const override;
  const AccountSnapshot& account() const override { return account_; }
  std::size_t duplicate_submissions() const override { return duplicates_; }

  const SimBrokerConfig& config() const { return config_; }

 private:
  enum class ActionKind { Ack, Reject, FillAttempt, Cancel };

  struct PendingAction {
    ActionKind kind = ActionKind::Ack;
    std::string order_id;
    std::int64_t due_ns = 0;
    std::int64_t sequence = 0;  // stable ordering for equal due times
  };

  void emit(OrderEvent event);
  Order* find_mutable(const std::string& order_id);
  void schedule(ActionKind kind, const std::string& order_id, std::int64_t due_ns);
  void process_action(const PendingAction& action, std::int64_t now_ns, market::Price bid,
                      market::Price ask);
  void apply_fill(Order& order, double units, double price, std::int64_t now_ns);
  double random_unit();  // deterministic PRNG in [0,1)

  SimBrokerConfig config_;
  std::vector<Order> orders_;
  std::vector<Fill> fills_;
  std::vector<OrderEvent> events_;
  std::deque<PendingAction> pending_;
  std::map<std::string, std::string> client_to_order_;
  AccountSnapshot account_;

  market::Price bid_ = 0;
  market::Price ask_ = 0;
  bool has_quote_ = false;

  std::uint64_t next_order_number_ = 1;
  std::uint64_t next_action_sequence_ = 0;
  std::size_t duplicates_ = 0;

  // splitmix64: small, fast, and fully deterministic across platforms.
  std::uint64_t rng_state_ = 0;
};

}  // namespace vectra::execution
