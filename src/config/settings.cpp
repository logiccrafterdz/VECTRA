// Configuration loading: JSON -> typed engine settings.
#include "vectra/config/settings.hpp"

#include <fstream>
#include <sstream>

namespace vectra::config {

namespace {

constexpr double kMsToNs = 1'000'000.0;

std::int64_t ms_to_ns(const Json* section, const char* key, std::int64_t fallback_ms) {
  if (section == nullptr) {
    return fallback_ms * 1'000'000;
  }
  const double ms = section->get_number(key, static_cast<double>(fallback_ms));
  return static_cast<std::int64_t>(ms * kMsToNs);
}

void apply_validation(const Json& root, backtest::ReplayConfig* replay) {
  const Json* v = root.get_object("validation");
  if (v == nullptr) {
    return;
  }
  replay->validation.max_spread =
      market::double_to_price(v->get_number("max_spread",
                                            market::price_to_double(replay->validation.max_spread)));
  replay->validation.stale_threshold_ns = ms_to_ns(v, "stale_threshold_ms", 5000);
  replay->validation.reorder_tolerance_ns = ms_to_ns(v, "reorder_tolerance_ms", 0);
  replay->validation.stale_gap_threshold_ns = ms_to_ns(v, "stale_gap_threshold_ms", 5000);
  replay->validation.future_skew_ns = ms_to_ns(v, "future_skew_ms", 1000);
  replay->validation.require_source_time =
      v->get_bool("require_source_time", replay->validation.require_source_time);
  replay->validation.require_sequence =
      v->get_bool("require_sequence", replay->validation.require_sequence);
}

void apply_behavior(const Json& root, backtest::ReplayConfig* replay) {
  const Json* b = root.get_object("behavior");
  if (b == nullptr) {
    return;
  }
  auto& c = replay->behavior;
  c.window_short_events = b->get_size("window_short_events", c.window_short_events);
  c.window_mid_events = b->get_size("window_mid_events", c.window_mid_events);
  c.window_long_events = b->get_size("window_long_events", c.window_long_events);
  c.vol_window_events = b->get_size("vol_window_events", c.vol_window_events);
  c.persistence_window_events =
      b->get_size("persistence_window_events", c.persistence_window_events);
  c.min_vol_samples = b->get_size("min_vol_samples", c.min_vol_samples);
  c.min_volatility = b->get_number("min_volatility", c.min_volatility);
  c.spread_warn_ratio = b->get_number("spread_warn_ratio", c.spread_warn_ratio);
  c.window_short_ns = ms_to_ns(b, "window_short_ms", 0);
  c.window_mid_ns = ms_to_ns(b, "window_mid_ms", 0);
  c.window_long_ns = ms_to_ns(b, "window_long_ms", 0);
  c.decel_lag_events = b->get_size("decel_lag_events", c.decel_lag_events);
}

void apply_exhaustion(const Json& root, backtest::ReplayConfig* replay) {
  const Json* e = root.get_object("exhaustion");
  if (e == nullptr) {
    return;
  }
  auto& c = replay->prediction.exhaustion;
  c.weight_extension = e->get_number("weight_extension", c.weight_extension);
  c.weight_deceleration = e->get_number("weight_deceleration", c.weight_deceleration);
  c.weight_response_fade = e->get_number("weight_response_fade", c.weight_response_fade);
  c.weight_quality = e->get_number("weight_quality", c.weight_quality);
  c.z_normalization = e->get_number("z_normalization", c.z_normalization);
  c.spread_reference_ratio = e->get_number("spread_reference_ratio", c.spread_reference_ratio);
}

void apply_prediction(const Json& root, backtest::ReplayConfig* replay) {
  const Json* p = root.get_object("prediction");
  if (p == nullptr) {
    return;
  }
  auto& c = replay->prediction;
  c.horizon_events = p->get_size("horizon_events", c.horizon_events);
  c.reversal_exhaustion_threshold =
      p->get_number("reversal_exhaustion_threshold", c.reversal_exhaustion_threshold);
  c.continuation_exhaustion_threshold =
      p->get_number("continuation_exhaustion_threshold", c.continuation_exhaustion_threshold);
  c.min_extension_z = p->get_number("min_extension_z", c.min_extension_z);
  c.min_persistence = p->get_number("min_persistence", c.min_persistence);
  c.max_persistence_for_reversal =
      p->get_number("max_persistence_for_reversal", c.max_persistence_for_reversal);
  c.min_confidence = p->get_number("min_confidence", c.min_confidence);
  c.max_expected_move_log = p->get_number("max_expected_move_log", c.max_expected_move_log);
  c.full_confidence_samples = p->get_size("full_confidence_samples", c.full_confidence_samples);
  c.require_mid_confirmation =
      p->get_bool("require_mid_confirmation", c.require_mid_confirmation);
  c.require_long_confirmation =
      p->get_bool("require_long_confirmation", c.require_long_confirmation);
  c.min_continuation_efficiency =
      p->get_number("min_continuation_efficiency", c.min_continuation_efficiency);
  c.max_reversal_efficiency =
      p->get_number("max_reversal_efficiency", c.max_reversal_efficiency);
  c.max_continuation_extension_z =
      p->get_number("max_continuation_extension_z", c.max_continuation_extension_z);
  c.continuation_move_scale =
      p->get_number("continuation_move_scale", c.continuation_move_scale);
  c.reversal_move_scale =
      p->get_number("reversal_move_scale", c.reversal_move_scale);
  c.ablation_mode = p->get_string("ablation_mode", c.ablation_mode);
}

void apply_risk(const Json& root, backtest::ReplayConfig* replay) {
  const Json* r = root.get_object("risk");
  if (r == nullptr) {
    return;
  }
  auto& c = replay->risk;
  c.enabled = r->get_bool("enabled", c.enabled);
  c.kill_switch = r->get_bool("kill_switch", c.kill_switch);
  c.min_expected_edge = r->get_number("min_expected_edge", c.min_expected_edge);
  c.commission = r->get_number("commission", c.commission);
  c.slippage = r->get_number("slippage", c.slippage);
  c.max_spread = r->get_number("max_spread", c.max_spread);
  c.round_trip_cost = r->get_bool("round_trip_cost", c.round_trip_cost);
  c.order_units = r->get_number("order_units", c.order_units);
  c.max_position_units = r->get_number("max_position_units", c.max_position_units);
  c.max_exposure = r->get_number("max_exposure", c.max_exposure);
  c.max_daily_loss = r->get_number("max_daily_loss", c.max_daily_loss);
  c.max_open_orders = static_cast<int>(r->get_size(
      "max_open_orders", static_cast<std::size_t>(c.max_open_orders < 0 ? 0 : c.max_open_orders)));
  c.max_orders_per_day =
      static_cast<int>(r->get_size("max_orders_per_day",
                                   static_cast<std::size_t>(
                                       c.max_orders_per_day < 0 ? 0 : c.max_orders_per_day)));
  c.cooldown_events = r->get_size("cooldown_events", c.cooldown_events);
  c.cooldown_ns = ms_to_ns(r, "cooldown_ms", 0);
  c.data_health_window = r->get_size("data_health_window", c.data_health_window);
  c.max_invalid_ratio = r->get_number("max_invalid_ratio", c.max_invalid_ratio);
  c.data_stale_gate_ns = ms_to_ns(r, "data_stale_gate_ms", 2000);
}

void apply_execution(const Json& root, backtest::ReplayConfig* replay) {
  const Json* e = root.get_object("execution");
  if (e == nullptr) {
    return;
  }
  auto& c = replay->broker;
  c.ack_latency_ns = ms_to_ns(e, "ack_latency_ms", 50);
  c.fill_latency_ns = ms_to_ns(e, "fill_latency_ms", 100);
  c.cancel_latency_ns = ms_to_ns(e, "cancel_latency_ms", 100);
  c.reject_probability = e->get_number("reject_probability", c.reject_probability);
  c.partial_fill_probability =
      e->get_number("partial_fill_probability", c.partial_fill_probability);
  c.partial_fill_fraction = e->get_number("partial_fill_fraction", c.partial_fill_fraction);
  c.slippage = e->get_number("slippage", c.slippage);
  c.commission_per_unit = e->get_number("commission_per_unit", c.commission_per_unit);
  c.seed = static_cast<std::uint64_t>(
      e->get_size("seed", static_cast<std::size_t>(c.seed)));
}

void apply_backtest(const Json& root, backtest::ReplayConfig* replay) {
  const Json* b = root.get_object("backtest");
  if (b == nullptr) {
    return;
  }
  replay->initial_capital = b->get_number("initial_capital", replay->initial_capital);
  replay->mark_with_mid = b->get_bool("mark_with_mid", replay->mark_with_mid);
  replay->min_latency_samples = b->get_size("min_latency_samples", replay->min_latency_samples);
  replay->max_equity_curve_points =
      b->get_size("max_equity_curve_points", replay->max_equity_curve_points);
  replay->hold_horizon_events =
      b->get_size("hold_horizon_events", replay->hold_horizon_events);
  replay->hold_horizon_ns = ms_to_ns(b, "hold_horizon_ms", 0);
  replay->eval_horizon_ns = ms_to_ns(b, "eval_horizon_ms", 0);
  replay->stop_loss_price =
      b->get_number("stop_loss_price", replay->stop_loss_price);
  replay->take_profit_price =
      b->get_number("take_profit_price", replay->take_profit_price);
  replay->retain_tick_records =
      b->get_bool("retain_tick_records", replay->retain_tick_records);
}

void apply_telemetry(const Json& root, backtest::ReplayConfig* replay) {
  const Json* t = root.get_object("telemetry");
  if (t == nullptr) {
    return;
  }
  replay->min_latency_samples = t->get_size("min_latency_samples", replay->min_latency_samples);
}

}  // namespace

Settings default_settings() { return Settings{}; }

bool overlay_settings(const Json& root, Settings* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "null settings target";
    }
    return false;
  }
  if (!root.is_object()) {
    if (error != nullptr) {
      *error = "top-level JSON value must be an object";
    }
    return false;
  }
  out->instrument = root.get_string("instrument", out->instrument);
  out->replay.validation.expected_symbol = out->instrument;

  apply_validation(root, &out->replay);
  apply_behavior(root, &out->replay);
  apply_exhaustion(root, &out->replay);
  apply_prediction(root, &out->replay);
  apply_risk(root, &out->replay);
  apply_execution(root, &out->replay);
  apply_backtest(root, &out->replay);
  apply_telemetry(root, &out->replay);
  return true;
}

bool load_settings(const std::string& path, Settings* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "null settings target";
    }
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error != nullptr) {
      *error = "cannot open config file: " + path;
    }
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();

  std::string parse_error;
  auto root = Json::parse(text, &parse_error);
  if (!root.has_value()) {
    if (error != nullptr) {
      *error = "JSON parse error in " + path + ": " + parse_error;
    }
    return false;
  }
  return overlay_settings(*root, out, error);
}

}  // namespace vectra::config
