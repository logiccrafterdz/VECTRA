// Scoring predictor implementation.
#include "vectra/prediction/scoring_predictor.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vectra::prediction {

const char* to_string(SignalKind k) {
  switch (k) {
    case SignalKind::Neutral:     return "Neutral";
    case SignalKind::Continuation:return "Continuation";
    case SignalKind::Reversal:    return "Reversal";
  }
  return "Unknown";
}

const char* to_string(RejectionReason r) {
  switch (r) {
    case RejectionReason::None:                return "None";
    case RejectionReason::InsufficientData:    return "InsufficientData";
    case RejectionReason::InvalidInput:        return "InvalidInput";
    case RejectionReason::StaleData:           return "StaleData";
    case RejectionReason::WideSpread:          return "WideSpread";
    case RejectionReason::LowVolatility:       return "LowVolatility";
    case RejectionReason::InsufficientEvidence:return "InsufficientEvidence";
    case RejectionReason::LowConfidence:       return "LowConfidence";
    case RejectionReason::NoRuleMatch:         return "NoRuleMatch";
  }
  return "Unknown";
}

namespace {

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

ScoringPredictor::ScoringPredictor(PredictionConfig config) : config_(std::move(config)) {
  if (config_.horizon_events == 0) {
    config_.horizon_events = 1;
  }
}

Prediction ScoringPredictor::predict(const behavior::BehaviorFeatures& f) const {
  Prediction p;
  p.time_ns = f.time_ns;
  p.event_index = f.event_index;
  p.horizon_events = config_.horizon_events;

  // ---- Data gates ---------------------------------------------------------
  if (f.flags.invalid_event) {
    p.rejection = RejectionReason::InvalidInput;
    p.note = "feature set carries an invalid event flag";
    return p;  // valid = false
  }
  if (!f.valid || f.flags.insufficient_history) {
    p.rejection = RejectionReason::InsufficientData;
    p.note = "insufficient history for configured windows";
    return p;
  }
  if (f.flags.stale_gap) {
    p.rejection = RejectionReason::StaleData;
    p.note = "stale gap in the quote stream";
    return p;
  }
  if (f.flags.wide_spread) {
    p.rejection = RejectionReason::WideSpread;
    p.note = "spread above the configured warn ratio";
    return p;
  }
  if (f.flags.low_volatility) {
    p.rejection = RejectionReason::LowVolatility;
    p.note = "rolling volatility is zero or too small to normalize against";
    return p;
  }

  p.valid = true;

  // ---- Exhaustion hypothesis score ---------------------------------------
  const behavior::ExhaustionResult exhaustion =
      behavior::compute_exhaustion(f, config_.exhaustion);
  p.exhaustion = exhaustion.score;

  const double abs_z = std::fabs(f.z_short);
  const double extension_gate =
      config_.min_extension_z > 0.0 ? clampd(abs_z / config_.min_extension_z, 0.0, 1.0) : 1.0;

  // ---- Transparent scores -------------------------------------------------
  const double persistence_positive = clampd(f.persistence, 0.0, 1.0);
  p.continuation_score =
      clampd((1.0 - exhaustion.score) * extension_gate * persistence_positive, 0.0, 1.0);
  p.reversal_score =
      clampd(exhaustion.score * extension_gate * (1.0 - persistence_positive), 0.0, 1.0);

  // ---- Confidence (uncalibrated data-quality composite) -------------------
  const double spread_reference =
      config_.exhaustion.spread_reference_ratio > 0.0
          ? config_.exhaustion.spread_reference_ratio
          : 1.0;
  const double spread_component = clampd(f.spread_ratio / spread_reference, 0.0, 1.0);
  double quality = 1.0 - 0.25 * spread_component;
  if (f.flags.low_activity) {
    quality -= 0.20;
  }
  quality = clampd(quality, 0.0, 1.0);

  const double full_samples =
      static_cast<double>(std::max<std::size_t>(2, config_.full_confidence_samples));
  const double sample_factor = clampd(static_cast<double>(f.vol_samples) / full_samples, 0.0, 1.0);
  p.confidence = clampd(quality * sample_factor * extension_gate, 0.0, 1.0);

  // ---- Rule ---------------------------------------------------------------
  const double disp = f.disp_short;
  const double abs_disp = std::fabs(disp);
  const double direction_sign = disp > 0.0 ? 1.0 : (disp < 0.0 ? -1.0 : 0.0);

  bool rule_fired = false;
  if (direction_sign != 0.0 && abs_z >= config_.min_extension_z) {
    if (exhaustion.score >= config_.reversal_exhaustion_threshold &&
        f.persistence <= config_.max_persistence_for_reversal) {
      p.kind = SignalKind::Reversal;
      p.direction = -direction_sign;
      rule_fired = true;
    } else if (exhaustion.score <= config_.continuation_exhaustion_threshold &&
               f.persistence >= config_.min_persistence) {
      p.kind = SignalKind::Continuation;
      p.direction = direction_sign;
      rule_fired = true;
    }
  }

  if (rule_fired && p.confidence < config_.min_confidence) {
    p.kind = SignalKind::Neutral;
    p.direction = 0.0;
    p.rejection = RejectionReason::LowConfidence;
    p.note = "confidence " + std::to_string(p.confidence) + " below threshold";
    return p;
  }

  if (p.kind == SignalKind::Neutral) {
    p.direction = 0.0;
    if (direction_sign == 0.0 || abs_z < config_.min_extension_z) {
      p.rejection = RejectionReason::InsufficientEvidence;
      p.note = "displacement not extended enough (|z|=" + std::to_string(abs_z) + ")";
    } else {
      p.rejection = RejectionReason::NoRuleMatch;
      p.note = "exhaustion " + std::to_string(exhaustion.score) +
               " inside the neutral band or persistence condition unmet";
    }
    p.expected_move_log = 0.0;
    return p;
  }

  // ---- Heuristic expected movement over the horizon -----------------------
  const double strength = p.kind == SignalKind::Reversal ? p.reversal_score : p.continuation_score;
  const double magnitude =
      clampd(abs_disp, 0.0, config_.max_expected_move_log);
  p.expected_move_log = p.direction * strength * magnitude;
  p.note = "heuristic, uncalibrated; scores are not probabilities";
  return p;
}

}  // namespace vectra::prediction
