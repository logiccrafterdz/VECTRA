// Behavioral pressure engine: rolling state derived from observed price
// events.
//
// Vocabulary discipline (see docs/data_limitations.md):
//   * Everything in BehaviorFeatures is an OBSERVABLE derived from quotes:
//     displacement, per-tick response, persistence, volatility, spread ratio.
//   * Latent behavioral interpretations (forced liquidation, stop cascades,
//     retail positioning) are NOT inferred and must never be labeled here.
//   * Tick counts are NOT volume: quotes carry no traded-volume information.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "vectra/behavior/rolling_stats.hpp"
#include "vectra/market/market_event.hpp"

namespace vectra::behavior {

struct BehaviorEngineConfig {
  // Observation windows, measured in EVENTS (ticks), not wall-clock time.
  std::size_t window_short_events = 8;
  std::size_t window_mid_events = 32;
  std::size_t window_long_events = 128;

  std::size_t vol_window_events = 256;      // log-return samples for volatility
  std::size_t persistence_window_events = 16;
  std::size_t min_vol_samples = 32;         // below this, volatility is unreliable

  double min_volatility = 1e-8;             // near-zero volatility guard (log-return std)
  double min_mid_price = 1e-9;              // non-positive midpoint guard
  double spread_warn_ratio = 5e-4;          // spread/mid above this flags wide spread
  std::size_t min_persistence_samples = 4;  // returns needed for a persistence estimate
};

// Non-fatal data-quality annotations produced by the engine.
struct DataQualityFlags {
  bool insufficient_history = false;
  bool low_volatility = false;
  bool wide_spread = false;
  bool low_activity = false;
  bool stale_gap = false;
  bool invalid_event = false;

  bool any() const {
    return insufficient_history || low_volatility || wide_spread || low_activity ||
           stale_gap || invalid_event;
  }
  // Conditions under which the prediction engine must refuse to emit a signal.
  bool blocks_signal() const {
    return insufficient_history || low_volatility || invalid_event;
  }
};

std::string describe(const DataQualityFlags& flags);

// Observable quantities only. No latent behavioral labels.
struct BehaviorFeatures {
  bool valid = false;  // false when displacement cannot be computed yet

  std::int64_t time_ns = 0;
  std::uint64_t event_index = 0;

  double mid = 0.0;
  double spread = 0.0;
  double spread_ratio = 0.0;

  // Log displacements over nested event windows: r_h(t) = log(mid_t / mid_{t-h}).
  double disp_short = 0.0;
  double disp_mid = 0.0;
  double disp_long = 0.0;
  double disp_delta = 0.0;        // r_short(t) - r_short(t-1): change across updates
  double disp_window_diff = 0.0;  // r_short(t) - r_mid(t): change across windows

  // Movement persistence: mean sign agreement of recent single-event returns
  // with the sign of the short displacement. Range [-1, 1].
  double persistence = 0.0;

  // Price response relative to observable activity (ticks in the window).
  // This is displacement per tick, NOT per unit of traded volume.
  double response_short = 0.0;
  double response_mid = 0.0;
  double response_delta = 0.0;   // response_short - response_mid
  double response_fade = 0.0;    // relative decline of |response|, clamped to [0,1]

  double volatility = 0.0;       // per-event log-return sample std
  double z_short = 0.0;          // disp_short / (volatility * sqrt(short window))
  double deceleration = 0.0;     // slowdown along the move direction, clamped [-1,1]

  std::size_t short_window_events = 0;
  std::size_t mid_window_events = 0;
  std::size_t long_window_events = 0;
  std::size_t vol_samples = 0;
  std::size_t persistence_samples = 0;

  DataQualityFlags flags;
};

class BehaviorEngine {
 public:
  explicit BehaviorEngine(BehaviorEngineConfig config);

  // Incorporates a validated event. Events with validity != Ok or a
  // non-positive midpoint are NOT ingested: features are marked invalid and
  // flagged so that bad data can never pollute the rolling state.
  const BehaviorFeatures& update(const market::MarketEvent& event);

  const BehaviorFeatures& features() const { return features_; }
  std::uint64_t event_count() const { return event_index_; }
  std::size_t history_size() const { return mids_.size(); }

  void reset();

  const BehaviorEngineConfig& config() const { return config_; }

 private:
  double mid_at(std::size_t back) const;  // 0 = current event

  BehaviorEngineConfig config_;
  std::size_t max_window_ = 0;

  std::deque<double> mids_;        // bounded to the longest window + slack
  RollingWindow vol_window_{1};    // consecutive-event log returns
  std::deque<double> recent_returns_;  // bounded to persistence window
  double prev_mid_ = 0.0;

  double prev_disp_short_ = 0.0;
  bool prev_disp_valid_ = false;

  std::uint64_t event_index_ = 0;
  BehaviorFeatures features_;
};

}  // namespace vectra::behavior
