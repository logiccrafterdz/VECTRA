// Prediction engine tests: data gates, scoring rules, neutral outcomes.
#include "test_util.hpp"
#include "vectra_test.hpp"

#include <cmath>

#include "vectra/prediction/scoring_predictor.hpp"

using vectra::behavior::BehaviorFeatures;
using vectra::prediction::Prediction;
using vectra::prediction::PredictionConfig;
using vectra::prediction::RejectionReason;
using vectra::prediction::ScoringPredictor;
using vectra::prediction::SignalKind;

namespace {

BehaviorFeatures base_features() {
  BehaviorFeatures features;
  features.valid = true;
  features.time_ns = 1'000'000'000;
  features.event_index = 100;
  features.mid = 1.10;
  features.spread = 0.0001;
  features.spread_ratio = 0.0001 / 1.10;
  features.disp_short = 0.0008;   // positive short-horizon move
  features.disp_mid = 0.0010;
  features.disp_long = 0.0012;
  features.persistence = 0.9;
  features.volatility = 1e-5;
  features.vol_samples = 64;
  features.z_short = 3.0;
  features.short_window_events = 8;
  features.mid_window_events = 32;
  return features;
}

}  // namespace

// ---------------------------------------------------------------------------
// Data gates: predictions must be rejected on bad inputs
// ---------------------------------------------------------------------------

VT_TEST(Prediction, InsufficientHistoryIsRejected) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.valid = false;
  features.flags.insufficient_history = true;
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_FALSE(prediction.valid);
  VT_ASSERT_EQ(prediction.kind, SignalKind::Neutral);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::InsufficientData);
  VT_ASSERT_FALSE(prediction.has_signal());
}

VT_TEST(Prediction, InvalidEventFlagIsRejected) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.valid = false;
  features.flags.invalid_event = true;
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_FALSE(prediction.valid);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::InvalidInput);
}

VT_TEST(Prediction, StaleGapIsRejected) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.flags.stale_gap = true;
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_FALSE(prediction.valid);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::StaleData);
}

VT_TEST(Prediction, WideSpreadIsRejected) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.flags.wide_spread = true;
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_FALSE(prediction.valid);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::WideSpread);
}

VT_TEST(Prediction, ZeroVolatilityIsRejected) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.flags.low_volatility = true;
  features.volatility = 0.0;
  features.z_short = 0.0;
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_FALSE(prediction.valid);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::LowVolatility);
}

// ---------------------------------------------------------------------------
// Scoring rules
// ---------------------------------------------------------------------------

VT_TEST(Prediction, PersistentExtendedMoveGivesContinuationCandidate) {
  ScoringPredictor predictor{PredictionConfig{}};
  const Prediction prediction = predictor.predict(base_features());
  VT_ASSERT_TRUE(prediction.valid);
  VT_ASSERT_EQ(prediction.kind, SignalKind::Continuation);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::None);
  VT_ASSERT_TRUE(prediction.has_signal());
  VT_ASSERT_TRUE(prediction.direction > 0.0);          // with the displacement
  VT_ASSERT_TRUE(prediction.expected_move_log > 0.0);  // same sign as direction
  VT_ASSERT_TRUE(prediction.continuation_score > 0.0);
  VT_ASSERT_TRUE(prediction.continuation_score <= 1.0);
  VT_ASSERT_TRUE(prediction.reversal_score <= 1.0);
  VT_ASSERT_TRUE(prediction.confidence > 0.0);
}

VT_TEST(Prediction, ExhaustedFadingMoveGivesReversalCandidate) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.persistence = 0.0;     // move has lost agreement
  features.deceleration = 1.0;    // strongly slowing along its direction
  features.response_fade = 1.0;   // per-tick response collapsed
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_TRUE(prediction.valid);
  VT_ASSERT_EQ(prediction.kind, SignalKind::Reversal);
  VT_ASSERT_TRUE(prediction.direction < 0.0);          // against the displacement
  VT_ASSERT_TRUE(prediction.expected_move_log < 0.0);
  VT_ASSERT_TRUE(prediction.reversal_score > 0.0);
  VT_ASSERT_TRUE(prediction.has_signal());
}

VT_TEST(Prediction, UnextendedMoveIsNeutral) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.z_short = 0.4;  // below min_extension_z (1.5)
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_TRUE(prediction.valid);
  VT_ASSERT_EQ(prediction.kind, SignalKind::Neutral);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::InsufficientEvidence);
  VT_ASSERT_FALSE(prediction.has_signal());
  VT_ASSERT_NEAR(prediction.expected_move_log, 0.0, 1e-15);
}

VT_TEST(Prediction, ScoresInNeutralBandStayNeutral) {
  ScoringPredictor predictor{PredictionConfig{}};
  BehaviorFeatures features = base_features();
  features.persistence = 0.9;   // blocks the reversal rule
  features.deceleration = 1.0;  // pushes exhaustion up, but below reversal threshold
  features.response_fade = 0.0;
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_TRUE(prediction.valid);
  VT_ASSERT_EQ(prediction.kind, SignalKind::Neutral);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::NoRuleMatch);
  VT_ASSERT_NEAR(prediction.direction, 0.0, 1e-15);
}

VT_TEST(Prediction, LowConfidenceSuppressesAnOtherwiseValidSignal) {
  PredictionConfig config;
  config.min_confidence = 0.99;  // impossible to reach with low activity
  ScoringPredictor predictor{config};
  BehaviorFeatures features = base_features();
  features.flags.low_activity = true;  // knocks 0.2 off the quality factor
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_TRUE(prediction.valid);
  VT_ASSERT_EQ(prediction.kind, SignalKind::Neutral);
  VT_ASSERT_EQ(prediction.rejection, RejectionReason::LowConfidence);
}

VT_TEST(Prediction, DeterministicForIdenticalFeatures) {
  ScoringPredictor predictor{PredictionConfig{}};
  const Prediction a = predictor.predict(base_features());
  const Prediction b = predictor.predict(base_features());
  VT_ASSERT_EQ(static_cast<int>(a.kind), static_cast<int>(b.kind));
  VT_ASSERT_NEAR(a.continuation_score, b.continuation_score, 0.0);
  VT_ASSERT_NEAR(a.reversal_score, b.reversal_score, 0.0);
  VT_ASSERT_NEAR(a.expected_move_log, b.expected_move_log, 0.0);
}

VT_TEST(Prediction, ExpectedMoveIsCapped) {
  PredictionConfig config;
  config.max_expected_move_log = 0.001;
  ScoringPredictor predictor{config};
  BehaviorFeatures features = base_features();
  features.disp_short = 0.05;  // absurd displacement on a 1.10 quote
  const Prediction prediction = predictor.predict(features);
  VT_ASSERT_TRUE(std::fabs(prediction.expected_move_log) <= 0.001 + 1e-15);
}

VT_TEST(Prediction, RegimeEfficiencyGatesContinuationInChop) {
  PredictionConfig config;
  config.min_continuation_efficiency = 0.50;
  ScoringPredictor predictor{config};

  BehaviorFeatures chop = base_features();
  chop.regime_efficiency = 0.20;
  const Prediction blocked = predictor.predict(chop);
  VT_ASSERT_EQ(blocked.kind, SignalKind::Neutral);
  VT_ASSERT_EQ(blocked.rejection, RejectionReason::NoRuleMatch);

  BehaviorFeatures trend = base_features();
  trend.regime_efficiency = 0.75;
  const Prediction allowed = predictor.predict(trend);
  VT_ASSERT_EQ(allowed.kind, SignalKind::Continuation);
}

VT_TEST(Prediction, MoveScalesAdjustExpectedMoveBySignalKind) {
  PredictionConfig base_cfg;
  ScoringPredictor base_pred{base_cfg};
  const Prediction p1 = base_pred.predict(base_features());
  VT_ASSERT_EQ(p1.kind, SignalKind::Continuation);

  PredictionConfig scaled_cfg;
  scaled_cfg.continuation_move_scale = 2.0;
  ScoringPredictor scaled_pred{scaled_cfg};
  const Prediction p2 = scaled_pred.predict(base_features());
  VT_ASSERT_EQ(p2.kind, SignalKind::Continuation);
  VT_ASSERT_NEAR(p2.expected_move_log, 2.0 * p1.expected_move_log, 1e-12);
}
