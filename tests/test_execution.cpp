// Execution tests: order state transitions, duplicate protection, simulated
// venue behaviour and account reconciliation.
#include "test_util.hpp"
#include "vectra_test.hpp"

#include "vectra/execution/execution_gateway.hpp"
#include "vectra/execution/order.hpp"
#include "vectra/execution/simulated_broker.hpp"

using vectra::execution::AccountSnapshot;
using vectra::execution::apply_transition;
using vectra::execution::can_transition;
using vectra::execution::IExecutionGateway;
using vectra::execution::is_terminal;
using vectra::execution::Order;
using vectra::execution::OrderEventType;
using vectra::execution::OrderRequest;
using vectra::execution::OrderState;
using vectra::execution::reconcile;
using vectra::execution::Side;
using vectra::execution::SimBrokerConfig;
using vectra::execution::SimulatedBroker;
using vectra::market::Price;
using vectra::test::make_quote;

namespace {

SimBrokerConfig instant_config() {
  SimBrokerConfig config;
  config.ack_latency_ns = 50'000'000;
  config.fill_latency_ns = 100'000'000;
  config.cancel_latency_ns = 100'000'000;
  config.reject_probability = 0.0;
  config.partial_fill_probability = 0.0;
  config.slippage = 0.00001;
  config.commission_per_unit = 0.0;
  config.seed = 7;
  return config;
}

OrderRequest buy_request(const std::string& client_id, double units = 100.0) {
  OrderRequest request;
  request.client_order_id = client_id;
  request.symbol = "EURUSD";
  request.side = Side::Buy;
  request.units = units;
  request.type = vectra::execution::OrderType::Market;
  request.created_ns = 0;
  return request;
}

}  // namespace

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

VT_TEST(OrderStates, LegalTransitionsAreAllowed) {
  VT_ASSERT_TRUE(can_transition(OrderState::Created, OrderState::Submitted));
  VT_ASSERT_TRUE(can_transition(OrderState::Submitted, OrderState::Acknowledged));
  VT_ASSERT_TRUE(can_transition(OrderState::Acknowledged, OrderState::PartiallyFilled));
  VT_ASSERT_TRUE(can_transition(OrderState::PartiallyFilled, OrderState::PartiallyFilled));
  VT_ASSERT_TRUE(can_transition(OrderState::PartiallyFilled, OrderState::Filled));
  VT_ASSERT_TRUE(can_transition(OrderState::Acknowledged, OrderState::CancelPending));
  VT_ASSERT_TRUE(can_transition(OrderState::CancelPending, OrderState::Cancelled));
  VT_ASSERT_TRUE(can_transition(OrderState::Submitted, OrderState::Rejected));
}

VT_TEST(OrderStates, IllegalTransitionsAreRefused) {
  VT_ASSERT_FALSE(can_transition(OrderState::Filled, OrderState::Acknowledged));
  VT_ASSERT_FALSE(can_transition(OrderState::Filled, OrderState::PartiallyFilled));
  VT_ASSERT_FALSE(can_transition(OrderState::Cancelled, OrderState::Submitted));
  VT_ASSERT_FALSE(can_transition(OrderState::Rejected, OrderState::Filled));
  VT_ASSERT_FALSE(can_transition(OrderState::Created, OrderState::Filled));
  VT_ASSERT_FALSE(can_transition(OrderState::CancelPending, OrderState::Acknowledged));
  VT_ASSERT_TRUE(is_terminal(OrderState::Filled));
  VT_ASSERT_TRUE(is_terminal(OrderState::Cancelled));
  VT_ASSERT_TRUE(is_terminal(OrderState::Rejected));
  VT_ASSERT_FALSE(is_terminal(OrderState::Acknowledged));
}

VT_TEST(OrderStates, ApplyTransitionLeavesOrderUntouchedOnFailure) {
  Order order;
  order.state = OrderState::Filled;
  std::string error;
  VT_ASSERT_FALSE(apply_transition(order, OrderState::Acknowledged, &error));
  VT_ASSERT_EQ(order.state, OrderState::Filled);
  VT_ASSERT_FALSE(error.empty());
  error.clear();
  VT_ASSERT_FALSE(apply_transition(order, OrderState::Cancelled, &error));  // terminal
  VT_ASSERT_EQ(order.state, OrderState::Filled);
}

// ---------------------------------------------------------------------------
// Simulated broker
// ---------------------------------------------------------------------------

VT_TEST(Broker, SubmitCreatesSubmittedOrder) {
  SimulatedBroker broker(instant_config());
  const auto order = broker.submit(buy_request("A-1"), 0);
  VT_ASSERT_TRUE(order.has_value());
  VT_ASSERT_EQ(order->state, OrderState::Submitted);
  VT_ASSERT_EQ(broker.orders().size(), std::size_t{1});
  VT_ASSERT_FALSE(order->order_id.empty());
}

VT_TEST(Broker, DuplicateClientOrderIdIsProtected) {
  SimulatedBroker broker(instant_config());
  const auto first = broker.submit(buy_request("A-1"), 0);
  const auto second = broker.submit(buy_request("A-1"), 1'000'000);
  VT_ASSERT_TRUE(first.has_value());
  VT_ASSERT_TRUE(second.has_value());
  VT_ASSERT_STREQ(first->order_id, second->order_id);  // idempotent, not two orders
  VT_ASSERT_EQ(broker.orders().size(), std::size_t{1});
  VT_ASSERT_EQ(broker.duplicate_submissions(), std::size_t{1});

  bool saw_duplicate = false;
  for (const auto& event : broker.drain_events()) {
    if (event.type == OrderEventType::Duplicate) {
      saw_duplicate = true;
    }
  }
  VT_ASSERT_TRUE(saw_duplicate);
}

VT_TEST(Broker, InvalidRequestIsRefused) {
  SimulatedBroker broker(instant_config());
  OrderRequest request = buy_request("");
  VT_ASSERT_FALSE(broker.submit(request, 0).has_value());
  request = buy_request("A-1", 0.0);  // zero units
  VT_ASSERT_FALSE(broker.submit(request, 0).has_value());
  VT_ASSERT_EQ(broker.orders().size(), std::size_t{0});
}

VT_TEST(Broker, VenueRejectionFollowsLatency) {
  SimBrokerConfig config = instant_config();
  config.reject_probability = 1.0;
  SimulatedBroker broker(config);
  broker.submit(buy_request("A-1"), 0);

  broker.on_market(make_quote(1.10, 0.00010, 40'000'000, 1));   // before ack
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Submitted);

  broker.on_market(make_quote(1.10, 0.00010, 50'000'000, 2));   // ack due
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Rejected);
  bool saw_reject = false;
  for (const auto& event : broker.drain_events()) {
    if (event.type == OrderEventType::Rejected) {
      saw_reject = true;
    }
  }
  VT_ASSERT_TRUE(saw_reject);
}

VT_TEST(Broker, FillHappensAfterLatencyAtTheAsk) {
  SimulatedBroker broker(instant_config());
  broker.submit(buy_request("A-1", 100.0), 0);

  broker.on_market(make_quote(1.10, 0.00010, 40'000'000, 1));  // before ack latency
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Submitted);
  VT_ASSERT_TRUE(broker.fills().empty());

  broker.on_market(make_quote(1.10, 0.00010, 99'999'999, 2));  // acked, fill not due
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Acknowledged);
  VT_ASSERT_TRUE(broker.fills().empty());

  broker.on_market(make_quote(1.10, 0.00010, 100'000'000, 3));  // fill due
  VT_ASSERT_EQ(broker.fills().size(), std::size_t{1});
  const auto& fill = broker.fills().front();
  VT_ASSERT_NEAR(fill.units, 100.0, 1e-12);
  // Buy lifts the ask plus the configured slippage.
  VT_ASSERT_NEAR(fill.price, 1.10005 + 0.00001, 1e-12);
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Filled);
  VT_ASSERT_NEAR(broker.account().position, 100.0, 1e-9);
}

VT_TEST(Broker, FillAttemptBeforeAckIsNotDropped) {
  // Regression: when the configured fill latency is shorter than the ack
  // latency, the fill action becomes due before the order is acknowledged.
  // The venue must still fill the order instead of silently dropping the
  // action and leaving the order stuck in Acknowledged forever.
  SimBrokerConfig config = instant_config();
  config.ack_latency_ns = 100'000'000;   // 100 ms
  config.fill_latency_ns = 10'000'000;   // 10 ms: sooner than the ack
  SimulatedBroker broker(config);
  broker.submit(buy_request("A-1", 100.0), 0);

  // A single market event well after both latencies have elapsed.
  broker.on_market(make_quote(1.10, 0.00010, 200'000'000, 1));

  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Filled);
  VT_ASSERT_EQ(broker.fills().size(), std::size_t{1});
  VT_ASSERT_NEAR(broker.account().position, 100.0, 1e-9);
}

VT_TEST(Broker, PartialFillLeavesOrderWorking) {
  SimBrokerConfig config = instant_config();
  config.partial_fill_probability = 1.0;
  config.partial_fill_fraction = 0.5;
  SimulatedBroker broker(config);
  broker.submit(buy_request("A-1", 100.0), 0);

  broker.on_market(make_quote(1.10, 0.00010, 100'000'000, 1));
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::PartiallyFilled);
  VT_ASSERT_NEAR(broker.orders().front().filled_units, 50.0, 1e-9);
  VT_ASSERT_EQ(broker.fills().size(), std::size_t{1});
  VT_ASSERT_NEAR(broker.fills().front().units, 50.0, 1e-9);
}

VT_TEST(Broker, RepeatedFillsAccumulateToFullFill) {
  SimBrokerConfig config = instant_config();
  config.partial_fill_probability = 1.0;
  config.partial_fill_fraction = 0.5;
  SimulatedBroker broker(config);
  broker.submit(buy_request("A-1", 100.0), 0);

  std::int64_t now = 100'000'000;
  std::size_t iterations = 0;
  while (!broker.orders().front().is_terminal() && iterations < 25) {
    broker.on_market(make_quote(1.10, 0.00010, now, static_cast<std::uint64_t>(1 + iterations)));
    now += 100'000'000;
    ++iterations;
  }
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Filled);
  VT_ASSERT_NEAR(broker.orders().front().filled_units, 100.0, 1e-9);
  VT_ASSERT_TRUE(broker.fills().size() >= 2);
  double accumulated = 0.0;
  for (const auto& fill : broker.fills()) {
    accumulated += fill.units;
  }
  VT_ASSERT_NEAR(accumulated, 100.0, 1e-9);
}

VT_TEST(Broker, CancelLifecycleWorks) {
  SimulatedBroker broker(instant_config());
  broker.submit(buy_request("A-1", 100.0), 0);
  broker.on_market(make_quote(1.10, 0.00010, 50'000'000, 1));  // acknowledged

  VT_ASSERT_TRUE(broker.cancel(broker.orders().front().order_id, 60'000'000));
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::CancelPending);
  VT_ASSERT_FALSE(broker.cancel(broker.orders().front().order_id, 70'000'000));  // already pending

  broker.on_market(make_quote(1.10, 0.00010, 160'000'000, 2));
  VT_ASSERT_EQ(broker.orders().front().state, OrderState::Cancelled);
  VT_ASSERT_TRUE(broker.fills().empty());

  // Terminal orders cannot be cancelled.
  VT_ASSERT_FALSE(broker.cancel(broker.orders().front().order_id, 200'000'000));
}

VT_TEST(Broker, InvalidQuotesDoNotDriveFills) {
  SimulatedBroker broker(instant_config());
  broker.submit(buy_request("A-1", 100.0), 0);
  auto quote = make_quote(1.10, 0.00010, 100'000'000, 1);
  quote.validity = vectra::market::Validity::WideSpread;
  broker.on_market(quote);
  VT_ASSERT_TRUE(broker.fills().empty());
}

// ---------------------------------------------------------------------------
// Account / reconciliation
// ---------------------------------------------------------------------------

VT_TEST(Account, RealizedPnlUsesAveragePrice) {
  SimBrokerConfig config = instant_config();
  config.slippage = 0.0;  // isolate accounting from the slippage model
  SimulatedBroker broker(config);
  broker.submit(buy_request("B-1", 10.0), 0);
  broker.on_market(make_quote(1.10, 0.0, 100'000'000, 1));  // fill at ask = 1.10
  VT_ASSERT_NEAR(broker.account().position, 10.0, 1e-9);
  VT_ASSERT_NEAR(broker.account().avg_price, 1.10, 1e-9);

  OrderRequest sell = buy_request("B-2", 10.0);
  sell.side = Side::Sell;
  broker.submit(sell, 200'000'000);
  broker.on_market(make_quote(1.11, 0.0, 300'000'000, 2));  // sell fills at bid = 1.11

  VT_ASSERT_NEAR(broker.account().position, 0.0, 1e-9);
  VT_ASSERT_NEAR(broker.account().realized_pnl, 0.10, 1e-9);  // (1.11 - 1.10) * 10
}

VT_TEST(Account, ReconciliationDetectsMismatches) {
  AccountSnapshot local;
  local.position = 100.0;
  local.cash = -110.0;
  AccountSnapshot broker = local;
  const auto matched = reconcile(local, broker, 1e-6);
  VT_ASSERT_TRUE(matched.matched);

  broker.position = 50.0;
  const auto mismatched = reconcile(local, broker, 1e-6);
  VT_ASSERT_FALSE(mismatched.matched);
  VT_ASSERT_TRUE(mismatched.detail.find("position") != std::string::npos);
}
