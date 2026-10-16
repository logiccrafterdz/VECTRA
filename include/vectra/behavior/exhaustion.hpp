// Movement-exhaustion score: an explicit, configurable research hypothesis.
//
// This score is NOT a measurement of trader behavior. It combines four
// observable components into a bounded scalar so that a falsifiable rule can
// be tested against data:
//
//   extension      : |displacement| relative to rolling volatility
//   deceleration   : change of the short-horizon movement along its direction
//   response_fade  : decline of per-tick price response between windows
//   quality_penalty: spread / activity / stale-gap penalties
//
// All weights and normalizers are UNCALIBRATED starting values; they are
// configuration, not established constants (see docs/mathematical_model.md).
#pragma once

#include <string>

#include "vectra/behavior/behavior_engine.hpp"

namespace vectra::behavior {

struct ExhaustionConfig {
  double weight_extension = 0.35;
  double weight_deceleration = 0.30;
  double weight_response_fade = 0.20;
  double weight_quality = 0.15;

  double z_normalization = 3.0;         // |z| that maps to extension = 1.0 (UNCALIBRATED)
  double spread_reference_ratio = 5e-4; // spread ratio that saturates the penalty (UNCALIBRATED)
  double quality_spread_weight = 0.6;   // UNCALIBRATED
  double quality_stale_weight = 0.2;    // UNCALIBRATED
  double quality_activity_weight = 0.2; // UNCALIBRATED
};

struct ExhaustionResult {
  bool valid = false;
  double score = 0.0;          // [0,1], combined movement-exhaustion score
  double extension = 0.0;      // [0,1]
  double deceleration = 0.0;   // [0,1], only the slowing (positive) part contributes
  double response_fade = 0.0;  // [0,1]
  double quality_penalty = 0.0;// [0,1]
  std::string note;
};

ExhaustionResult compute_exhaustion(const BehaviorFeatures& features,
                                    const ExhaustionConfig& config);

}  // namespace vectra::behavior
