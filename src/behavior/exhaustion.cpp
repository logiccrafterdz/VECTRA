// Movement-exhaustion score implementation.
#include "vectra/behavior/exhaustion.hpp"

#include <cmath>

namespace vectra::behavior {

namespace {

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

ExhaustionResult compute_exhaustion(const BehaviorFeatures& features,
                                    const ExhaustionConfig& config) {
  ExhaustionResult result;
  if (!features.valid) {
    result.valid = false;
    result.note = "features not valid";
    return result;
  }
  result.valid = true;

  const double z_norm = config.z_normalization > 0.0 ? config.z_normalization : 1.0;
  result.extension = clampd(std::fabs(features.z_short) / z_norm, 0.0, 1.0);
  result.deceleration = clampd(features.deceleration, 0.0, 1.0);
  result.response_fade = clampd(features.response_fade, 0.0, 1.0);

  // Spread enters relative to a baseline: half the reference ratio counts as
  // "normal" market spread and contributes nothing. The penalty only grows
  // for genuinely elevated spreads, so ordinary 1-pip quotes never inflate
  // the exhaustion score.
  const double spread_baseline = config.spread_reference_ratio * 0.5;
  const double spread_component =
      spread_baseline > 0.0
          ? clampd((features.spread_ratio - spread_baseline) / spread_baseline, 0.0, 1.0)
          : 0.0;
  const double stale_component = features.flags.stale_gap ? 1.0 : 0.0;
  const double activity_component = features.flags.low_activity ? 1.0 : 0.0;
  result.quality_penalty =
      clampd(config.quality_spread_weight * spread_component +
                 config.quality_stale_weight * stale_component +
                 config.quality_activity_weight * activity_component,
             0.0, 1.0);

  const double weight_sum = config.weight_extension + config.weight_deceleration +
                            config.weight_response_fade + config.weight_quality;
  if (weight_sum <= 0.0) {
    result.score = 0.0;
    result.note = "all weights are zero";
    return result;
  }

  const double raw =
      (config.weight_extension * result.extension +
       config.weight_deceleration * result.deceleration +
       config.weight_response_fade * result.response_fade +
       config.weight_quality * result.quality_penalty) /
      weight_sum;

  result.score = clampd(raw, 0.0, 1.0);
  result.note = "research hypothesis score (uncalibrated)";
  return result;
}

}  // namespace vectra::behavior
