// Risk and decision engine tests: limits, kill switch, data-health gates.
#include "test_util.hpp"
#include "vectra_test.hpp"

#include "vectra/risk/risk_engine.hpp"

using vectra::prediction::Prediction;
using vectra::prediction::RejectionReason;
using vectra::prediction::SignalKind;
using vectra::risk::Decision;
using vectra::risk::DecisionAction;
using vectra::risk::RejectReason;
using vectra::risk::RiskConfig;
using vectra::risk::RiskEngine;
using vectra::test::make_quote;

namespace {

Prediction good_signal(double expected_move_log = 0.001) {
  Prediction prediction;
  prediction.valid = true;
  prediction.kind = SignalKind::Continuation;
  prediction.rejection = RejectionReason::None;
  prediction.direction = 1.0;
  prediction.expected_move_log = expected_move_log;
  prediction.confidence = 0.8;
  prediction.time_ns = 1'000'000'000;
  return prediction;
}

Prediction neutral_signal() {
  Prediction prediction;
  prediction.valid = true;
  prediction.kind = SignalKind::Neutral;
  prediction.rejection = RejectionReason::InsufficientEvidence;
  prediction.direction = 0.0;
  prediction.expected_move_log = 0.0;
  return prediction;
}

}  // namespace

VT_TEST(Risk, AllowsSaneSignalWithFreshData) {
  RiskEngine risk{RiskConfig{}};
  const Decision decision = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(decision.action, DecisionAction::Allow);
  VT_ASSERT_EQ(decision.reason, RejectReason::None);
  VT_ASSERT_TRUE(decision.edge > 0.0);
  VT_ASSERT_NEAR(decision.size_units, 5000.0, 1e-9);  // configured order size
}

VT_TEST(Risk, KillSwitchBlocksEverything) {
  RiskConfig config;
  config.kill_switch = true;
  RiskEngine risk{config};
  const Decision decision = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(decision.action, DecisionAction::Reject);
  VT_ASSERT_EQ(decision.reason, RejectReason::KillSwitch);

  risk.set_kill_switch(false);
  const Decision after = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 2), 2);
  VT_ASSERT_EQ(after.action, DecisionAction::Allow);
}

VT_TEST(Risk, DisabledEngineRejects) {
  RiskConfig config;
  config.enabled = false;
  RiskEngine risk{config};
  const Decision decision = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(decision.reason, RejectReason::RiskDisabled);
}

VT_TEST(Risk, DailyLossLimitStopsNewRiskAndResetsOnNewDay) {
  RiskConfig config;
  config.max_daily_loss = 250.0;
  RiskEngine risk{config};

  risk.record_realized_pnl(-250.0);
  const Decision blocked = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(blocked.reason, RejectReason::DailyLossLimit);

  // Same day: no reset.
  risk.on_new_day(0);
  VT_ASSERT_EQ(risk.daily_realized_pnl(), -250.0);

  // Next day: limits clear.
  risk.on_new_day(1);
  VT_ASSERT_NEAR(risk.daily_realized_pnl(), 0.0, 1e-15);
  const Decision allowed = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 2), 2);
  VT_ASSERT_EQ(allowed.action, DecisionAction::Allow);
}

VT_TEST(Risk, InvalidAndNeutralPredictionsAreRejected) {
  RiskEngine risk{RiskConfig{}};
  Prediction invalid = good_signal();
  invalid.valid = false;
  invalid.rejection = RejectionReason::StaleData;
  const Decision invalid_decision = risk.evaluate(invalid, make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(invalid_decision.reason, RejectReason::InvalidSignal);

  const Decision neutral = risk.evaluate(neutral_signal(), make_quote(1.10, 0.00010, 2), 2);
  VT_ASSERT_EQ(neutral.reason, RejectReason::NoSignal);
}

VT_TEST(Risk, InsufficientEdgeIsRejected) {
  RiskEngine risk{RiskConfig{}};
  // Expected move far below spread + commission + slippage.
  const Decision decision =
      risk.evaluate(good_signal(1e-6), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(decision.reason, RejectReason::InsufficientEdge);
  VT_ASSERT_TRUE(decision.edge < decision.cost);
}

VT_TEST(Risk, WideSpreadIsRejectedAsCost) {
  RiskEngine risk{RiskConfig{}};
  const Decision decision =
      risk.evaluate(good_signal(), make_quote(1.10, 0.00045, 1), 1);  // > max_spread
  VT_ASSERT_EQ(decision.reason, RejectReason::CostTooHigh);
}

VT_TEST(Risk, MaxPositionLimitIsEnforced) {
  RiskConfig config;
  config.max_position_units = 50000.0;
  RiskEngine risk{config};
  risk.record_position(50000.0);
  const Decision decision = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(decision.reason, RejectReason::MaxPosition);
}

VT_TEST(Risk, MaxExposureLimitIsEnforced) {
  RiskConfig config;
  config.max_position_units = 1'000'000.0;   // position limit not binding
  config.max_exposure = 5'000.0;             // quote notional binding
  RiskEngine risk{config};
  risk.record_position(5'000.0);             // already at the exposure ceiling
  const Decision decision = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(decision.reason, RejectReason::MaxExposure);
}

VT_TEST(Risk, PositionHeadroomCapsOrderSize) {
  RiskConfig config;
  config.max_position_units = 10000.0;
  config.order_units = 5000.0;
  RiskEngine risk{config};
  risk.record_position(7000.0);
  const Decision decision = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(decision.action, DecisionAction::Allow);
  VT_ASSERT_NEAR(decision.size_units, 3000.0, 1e-9);
}

VT_TEST(Risk, OpenOrderAndDailyOrderLimitsAreEnforced) {
  RiskConfig config;
  config.max_open_orders = 2;
  config.max_orders_per_day = 1;
  RiskEngine risk{config};

  risk.record_open_orders(2);
  const Decision open_blocked = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(open_blocked.reason, RejectReason::MaxOpenOrders);

  RiskEngine fresh{config};
  fresh.record_order_placed();
  const Decision daily_blocked = fresh.evaluate(good_signal(), make_quote(1.10, 0.00010, 1), 1);
  VT_ASSERT_EQ(daily_blocked.reason, RejectReason::MaxOrdersPerDay);
}

VT_TEST(Risk, HighInvalidRatioMarksDataUnreliable) {
  RiskConfig config;
  config.data_health_window = 10;
  config.max_invalid_ratio = 0.2;
  RiskEngine risk{config};
  for (int i = 0; i < 5; ++i) {
    risk.record_data_point(false, 1'000'000'000);
    risk.record_data_point(true, 1'000'000'000);
  }
  VT_ASSERT_FALSE(risk.data_reliable());
  const Decision decision = risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 1),
                                          1'000'000'000);
  VT_ASSERT_EQ(decision.reason, RejectReason::DataUnreliable);
}

VT_TEST(Risk, StaleDataStreamMarksDataUnreliable) {
  RiskConfig config;
  config.data_stale_gate_ns = 2'000'000'000;
  RiskEngine risk{config};
  risk.record_data_point(true, 1'000'000'000);
  const Decision decision =
      risk.evaluate(good_signal(), make_quote(1.10, 0.00010, 10'000'000'000), 10'000'000'000);
  VT_ASSERT_EQ(decision.reason, RejectReason::DataUnreliable);
}

VT_TEST(Risk, DataHealthStartsReliable) {
  RiskEngine risk{RiskConfig{}};
  VT_ASSERT_TRUE(risk.data_reliable());
}
