// Prediction types and configuration.
//
// Calibration policy: VECTRA v0.1 does NOT claim calibrated probabilities.
// Every value in Prediction is therefore named and reported as a SCORE in
// [0,1], not a probability. If a future version is statistically calibrated
// against held-out data, it may start calling them probabilities.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "vectra/behavior/exhaustion.hpp"

namespace vectra::prediction {

enum class SignalKind {
  Neutral = 0,      // no tradeable signal
  Continuation,     // candidate: displacement continues in its current direction
  Reversal,         // candidate: displacement shows signs of exhausting
};

const char* to_string(SignalKind k);

enum class RejectionReason {
  None = 0,
  InsufficientData,   // not enough history for the configured windows
  InvalidInput,       // features marked invalid by the behavior engine
  StaleData,          // quote stream shows a stale gap
  WideSpread,         // spread beyond the configured warn ratio
  LowVolatility,      // rolling volatility ~ 0: no meaningful normalization
  InsufficientEvidence, // move not extended/persistent enough for any rule
  LowConfidence,      // data-quality driven confidence below threshold
  NoRuleMatch,        // scores fell inside the neutral band
};

const char* to_string(RejectionReason r);

struct PredictionConfig {
  std::size_t horizon_events = 8;  // prediction horizon in events (ticks)

  // --- Rule thresholds: UNCALIBRATED starting values, all configurable. ---
  double reversal_exhaustion_threshold = 0.65;
  double continuation_exhaustion_threshold = 0.45;
  double min_extension_z = 1.5;
  double min_persistence = 0.25;
  double max_persistence_for_reversal = 0.50;
  double min_confidence = 0.30;
  double max_expected_move_log = 0.004;
  // Volatility samples at (or above) which the sample-size factor of the
  // confidence score reaches 1.0. UNCALIBRATED starting value.
  std::size_t full_confidence_samples = 64;

  behavior::ExhaustionConfig exhaustion;
};

struct Prediction {
  // Data gates: false => the prediction must never be used (stale, invalid,
  // insufficient history, zero volatility, oversized spread).
  bool valid = false;

  SignalKind kind = SignalKind::Neutral;
  RejectionReason rejection = RejectionReason::None;

  std::int64_t time_ns = 0;
  std::uint64_t event_index = 0;
  std::size_t horizon_events = 0;

  double continuation_score = 0.0;  // [0,1] score, NOT a calibrated probability
  double reversal_score = 0.0;      // [0,1] score, NOT a calibrated probability
  double confidence = 0.0;          // [0,1] uncalibrated confidence score
  double exhaustion = 0.0;          // [0,1] movement-exhaustion hypothesis score
  double direction = 0.0;           // -1, 0 or +1 (0 for Neutral)
  double expected_move_log = 0.0;   // signed heuristic log-return over the horizon

  std::string note;

  // A tradable signal exists only when all three hold.
  bool has_signal() const {
    return valid && kind != SignalKind::Neutral && rejection == RejectionReason::None;
  }
};

}  // namespace vectra::prediction
