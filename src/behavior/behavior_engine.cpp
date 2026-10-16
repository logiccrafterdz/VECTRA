// Behavioral pressure engine implementation.
#include "vectra/behavior/behavior_engine.hpp"

#include <cmath>
#include <utility>

namespace vectra::behavior {

namespace {

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double sign_of(double v) { return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0); }

}  // namespace

std::string describe(const DataQualityFlags& f) {
  std::string out;
  const auto add = [&out](const char* name) {
    if (!out.empty()) {
      out += ",";
    }
    out += name;
  };
  if (f.insufficient_history) add("insufficient_history");
  if (f.low_volatility) add("low_volatility");
  if (f.wide_spread) add("wide_spread");
  if (f.low_activity) add("low_activity");
  if (f.stale_gap) add("stale_gap");
  if (f.invalid_event) add("invalid_event");
  return out.empty() ? std::string("none") : out;
}

BehaviorEngine::BehaviorEngine(BehaviorEngineConfig config) : config_(std::move(config)) {
  if (config_.window_short_events < 1) {
    config_.window_short_events = 1;
  }
  if (config_.window_mid_events < config_.window_short_events) {
    config_.window_mid_events = config_.window_short_events;
  }
  if (config_.window_long_events < config_.window_mid_events) {
    config_.window_long_events = config_.window_mid_events;
  }
  if (config_.vol_window_events < 2) {
    config_.vol_window_events = 2;
  }
  if (config_.persistence_window_events < 1) {
    config_.persistence_window_events = 1;
  }
  if (config_.min_vol_samples < 2) {
    config_.min_vol_samples = 2;
  }
  max_window_ = config_.window_long_events + 2;
  vol_window_ = RollingWindow(config_.vol_window_events);
  reset();
}

void BehaviorEngine::reset() {
  mids_.clear();
  vol_window_.clear();
  recent_returns_.clear();
  prev_mid_ = 0.0;
  prev_disp_short_ = 0.0;
  prev_disp_valid_ = false;
  event_index_ = 0;
  features_ = BehaviorFeatures{};
}

double BehaviorEngine::mid_at(std::size_t back) const {
  if (back >= mids_.size()) {
    return 0.0;
  }
  return mids_[mids_.size() - 1 - back];
}

const BehaviorFeatures& BehaviorEngine::update(const market::MarketEvent& event) {
  const double mid = event.mid_price();

  // Defensive: never ingest invalid or non-positive quotes into rolling state.
  if (event.validity != market::Validity::Ok || mid <= config_.min_mid_price) {
    features_ = BehaviorFeatures{};
    features_.valid = false;
    features_.time_ns = event.source_time_ns;
    features_.event_index = event_index_;
    features_.flags.invalid_event = true;
    return features_;
  }

  ++event_index_;
  mids_.push_back(mid);
  while (mids_.size() > max_window_) {
    mids_.pop_front();
  }

  if (prev_mid_ > 0.0) {
    const double r = std::log(mid / prev_mid_);
    if (std::isfinite(r)) {
      vol_window_.push(r);
      recent_returns_.push_back(r);
      while (recent_returns_.size() > config_.persistence_window_events) {
        recent_returns_.pop_front();
      }
    }
  }
  prev_mid_ = mid;

  BehaviorFeatures f;
  f.time_ns = event.source_time_ns;
  f.event_index = event_index_;
  f.mid = mid;
  f.spread = market::price_to_double(event.spread());
  f.spread_ratio = mid > 0.0 ? f.spread / mid : 0.0;
  f.short_window_events = config_.window_short_events;
  f.mid_window_events = config_.window_mid_events;
  f.long_window_events = config_.window_long_events;
  f.vol_samples = vol_window_.size();
  f.persistence_samples = std::min(config_.persistence_window_events, recent_returns_.size());
  f.volatility = vol_window_.stddev();

  f.flags.wide_spread = f.spread_ratio > config_.spread_warn_ratio;
  f.flags.low_volatility =
      (f.vol_samples < config_.min_vol_samples) || (f.volatility <= config_.min_volatility);
  f.flags.stale_gap = market::has_flag(event.flags, market::EventFlags::StaleGap);

  if (mids_.size() < config_.window_long_events + 1) {
    f.flags.insufficient_history = true;
    f.valid = false;
    prev_disp_valid_ = false;
    features_ = f;
    return features_;
  }

  const double mid_now = mid_at(0);
  f.disp_short = std::log(mid_now / mid_at(config_.window_short_events));
  f.disp_mid = std::log(mid_now / mid_at(config_.window_mid_events));
  f.disp_long = std::log(mid_now / mid_at(config_.window_long_events));
  f.disp_window_diff = f.disp_short - f.disp_mid;
  f.disp_delta = prev_disp_valid_ ? (f.disp_short - prev_disp_short_) : 0.0;

  // Persistence: mean sign agreement of recent single-event returns with the
  // direction of the current short displacement.
  if (f.persistence_samples >= config_.min_persistence_samples &&
      std::fabs(f.disp_short) > 0.0) {
    const double dir = sign_of(f.disp_short);
    double acc = 0.0;
    for (std::size_t i = 0; i < f.persistence_samples; ++i) {
      const double r = recent_returns_[recent_returns_.size() - 1 - i];
      acc += sign_of(r) * dir;
    }
    f.persistence = clampd(acc / static_cast<double>(f.persistence_samples), -1.0, 1.0);
  } else {
    f.persistence = 0.0;
  }

  // Price response relative to observable activity: displacement per tick in
  // the window. Tick count is not traded volume.
  const double n_short = static_cast<double>(config_.window_short_events);
  const double n_mid = static_cast<double>(config_.window_mid_events);
  f.response_short = f.disp_short / n_short;
  f.response_mid = f.disp_mid / n_mid;
  f.response_delta = f.response_short - f.response_mid;

  constexpr double kResponseEpsilon = 1e-15;
  if (std::fabs(f.response_short) < kResponseEpsilon &&
      std::fabs(f.response_mid) < kResponseEpsilon) {
    f.response_fade = 0.0;
    f.flags.low_activity = true;
  } else {
    f.response_fade = clampd(
        1.0 - std::fabs(f.response_short) / (std::fabs(f.response_mid) + kResponseEpsilon),
        0.0, 1.0);
  }
  if (f.persistence_samples < config_.min_persistence_samples) {
    f.flags.low_activity = true;
  }

  const double vol_scale = f.volatility * std::sqrt(static_cast<double>(config_.window_short_events));
  if (!f.flags.low_volatility && vol_scale > 0.0) {
    f.z_short = f.disp_short / vol_scale;
    if (prev_disp_valid_) {
      f.deceleration = clampd(sign_of(f.disp_short) * (prev_disp_short_ - f.disp_short) /
                                  vol_scale,
                              -1.0, 1.0);
    }
  }

  f.valid = true;
  prev_disp_short_ = f.disp_short;
  prev_disp_valid_ = true;
  features_ = f;
  return features_;
}

}  // namespace vectra::behavior
