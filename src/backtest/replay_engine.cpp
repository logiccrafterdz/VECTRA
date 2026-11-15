// Event-driven replay engine implementation.
#include "vectra/backtest/replay_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace vectra::backtest {

namespace {

constexpr std::int64_t kNanosPerDay = 86'400'000'000'000LL;

market::ValidatorConfig make_validation_config(const ReplayConfig& config) {
  market::ValidatorConfig v = config.validation;
  if (!config.instrument.empty()) {
    v.expected_symbol = config.instrument;
  }
  return v;
}

// Independent local account bookkeeping (used as the reconciliation counter
// part of the broker's internal account).
void apply_fill_to(execution::AccountSnapshot& account, execution::Side side, double units,
                   double price, double commission) {
  const double signed_units = side == execution::Side::Buy ? units : -units;
  if (account.position == 0.0 || account.position * signed_units > 0.0) {
    const double weight_old = std::fabs(account.position);
    account.avg_price =
        (weight_old * account.avg_price + units * price) / (weight_old + units);
    account.position += signed_units;
  } else {
    const double closing = std::min(units, std::fabs(account.position));
    const double per_unit =
        account.position > 0.0 ? (price - account.avg_price) : (account.avg_price - price);
    account.realized_pnl += per_unit * closing;
    const double new_position = account.position + signed_units;
    if (new_position == 0.0) {
      account.position = 0.0;
      account.avg_price = 0.0;
    } else if ((new_position > 0.0) == (account.position > 0.0)) {
      account.position = new_position;
    } else {
      account.position = new_position;
      account.avg_price = price;
    }
  }
  account.cash -= signed_units * price;
  account.commission_paid += commission;
  account.cash -= commission;
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    hash ^= (value >> (i * 8)) & 0xFFULL;
    hash *= 1099511628211ULL;  // FNV-1a prime
  }
}

std::uint64_t hash_double(std::uint64_t hash, double value) {
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(value));
  hash_u64(hash, bits);
  return hash;
}

std::size_t count_open_orders(const std::vector<execution::Order>& orders) {
  std::size_t open = 0;
  for (const auto& order : orders) {
    if (!order.is_terminal()) {
      ++open;
    }
  }
  return open;
}

}  // namespace

ReplayEngine::ReplayEngine(ReplayConfig config)
    : config_(std::move(config)),
      validator_(make_validation_config(config_)),
      behavior_(config_.behavior),
      predictor_(config_.prediction),
      risk_(config_.risk),
      broker_(config_.broker) {
  stats_.initial_equity = config_.initial_capital;
  equity_peak_ = config_.initial_capital;
  event_latency_.set_min_samples(config_.min_latency_samples);
  signal_latency_.set_min_samples(config_.min_latency_samples);
  decision_latency_.set_min_samples(config_.min_latency_samples);
  order_latency_.set_min_samples(config_.min_latency_samples);
}

void ReplayEngine::apply_local_fill(const execution::OrderEvent& event,
                                    double half_spread_price) {
  const double commission = event.units * config_.broker.commission_per_unit;
  const double slippage = event.units * config_.broker.slippage;

  // Informational: the half-spread paid by crossing at this moment. It is
  // already embedded in gross and net P&L (fills never happen at mid), so it
  // is reported separately and never added twice.
  stats_.spread_cost_total += event.units * half_spread_price;

  const double position_before = local_account_.position;
  const double realized_before = local_account_.realized_pnl;
  apply_fill_to(local_account_, event.side, event.units, event.price, commission);
  if (local_account_.position == 0.0 || position_before == 0.0 ||
      ((position_before > 0.0) != (local_account_.position > 0.0))) {
    position_held_events_ = 0;
    position_opened_time_ns_ = event.time_ns;
  }
  // Feed the realized P&L of this fill back into the risk engine so that the
  // daily-loss gate sees closing P&L (previously it never did during replay).
  const double realized_delta = local_account_.realized_pnl - realized_before;
  if (realized_delta != 0.0) {
    risk_.record_realized_pnl(realized_delta);
  }

  // Gross bookkeeping: same execution price minus the modeled slippage, and
  // no commission, so gross vs net isolates the transaction costs.
  const double gross_price =
      event.side == execution::Side::Buy ? event.price - config_.broker.slippage
                                         : event.price + config_.broker.slippage;
  apply_fill_to(gross_account_, event.side, event.units, gross_price, 0.0);

  commission_paid_ += commission;
  slippage_paid_ += slippage;

  if (config_.stream_decisions) {
    std::printf(
        "{\"event\":\"FILL\",\"time_ns\":%lld,\"order_id\":\"%s\",\"side\":\"%s\","
        "\"units\":%.2f,\"price\":%.5f,\"position\":%.2f,\"realized_pnl\":%.2f}\n",
        static_cast<long long>(event.time_ns), event.order_id.c_str(),
        execution::to_string(event.side), event.units, event.price,
        local_account_.position, local_account_.realized_pnl);
    std::fflush(stdout);
  }
}

void ReplayEngine::handle_order_events(std::int64_t now_ns, market::Price bid,
                                       market::Price ask) {
  (void)now_ns;
  const double half_spread = market::price_to_double(ask - bid) * 0.5;
  std::vector<execution::OrderEvent> events = broker_.drain_events();
  for (const auto& event : events) {
    order_events_.push_back(event);
    switch (event.type) {
      case execution::OrderEventType::Acknowledged: {
        const auto it = submit_times_.find(event.order_id);
        if (it != submit_times_.end()) {
          order_latency_.record(event.time_ns - it->second);
        }
        break;
      }
      case execution::OrderEventType::Rejected:
        ++stats_.orders_venue_rejected;
        break;
      case execution::OrderEventType::Cancelled:
        ++stats_.orders_cancelled;
        break;
      case execution::OrderEventType::Duplicate:
        ++stats_.orders_duplicate;
        break;
      case execution::OrderEventType::PartialFill:
        ++stats_.partial_fills;
        ++stats_.fills;
        apply_local_fill(event, half_spread);
        break;
      case execution::OrderEventType::Filled:
        ++stats_.fills;
        apply_local_fill(event, half_spread);
        break;
      case execution::OrderEventType::CancelRejected:
        break;
    }
  }

  risk_.record_open_orders(static_cast<int>(count_open_orders(broker_.orders())));
  risk_.record_position(local_account_.position);
}

void ReplayEngine::update_equity(market::Price bid, market::Price ask,
                                 std::int64_t now_ns) {
  const double bid_price = market::price_to_double(bid);
  const double ask_price = market::price_to_double(ask);
  double mark = (bid_price + ask_price) * 0.5;
  if (!config_.mark_with_mid) {
    if (local_account_.position > 0.0) {
      mark = bid_price;  // long marked at the bid (conservative)
    } else if (local_account_.position < 0.0) {
      mark = ask_price;  // short marked at the ask (conservative)
    }
  }

  const double net_equity =
      config_.initial_capital + local_account_.cash + local_account_.position * mark;
  const double gross_equity =
      config_.initial_capital + gross_account_.cash + gross_account_.position * mark;

  if (net_equity > equity_peak_) {
    equity_peak_ = net_equity;
  }
  stats_.peak_equity = equity_peak_;
  const double drawdown = equity_peak_ - net_equity;
  if (drawdown > stats_.max_drawdown) {
    stats_.max_drawdown = drawdown;
    stats_.max_drawdown_pct =
        equity_peak_ > 0.0 ? 100.0 * drawdown / equity_peak_ : 0.0;
  }
  stats_.final_equity = net_equity;
  stats_.net_pnl = net_equity - config_.initial_capital;
  stats_.gross_pnl = gross_equity - config_.initial_capital;

  // Bounded, downsampled equity curve for the report.
  if (equity_curve_.size() >= config_.max_equity_curve_points) {
    std::vector<std::pair<std::int64_t, double>> thinned;
    thinned.reserve(equity_curve_.size() / 2 + 1);
    for (std::size_t i = 0; i < equity_curve_.size(); i += 2) {
      thinned.push_back(equity_curve_[i]);
    }
    equity_curve_.swap(thinned);
  }
  if (equity_samples_ % 2 == 0 || equity_curve_.empty()) {
    equity_curve_.emplace_back(now_ns, net_equity);
  }
  ++equity_samples_;
}

void ReplayEngine::evaluate_matured_signals(std::uint64_t current_event_index,
                                            std::int64_t current_time_ns, double mid,
                                            double bid, double ask) {
  const double round_trip_friction =
      2.0 * (config_.risk.commission + config_.risk.slippage);

  const auto accumulate = [round_trip_friction](SignalHorizonStats& s, double signed_mid,
                                                double net_spread, bool is_non_overlapping) {
    ++s.evaluated;
    if (signed_mid > 0.0) {
      ++s.hits;
    } else if (signed_mid == 0.0) {
      ++s.flats;
    }
    const double net_friction = net_spread - round_trip_friction;
    s.sum_signed_mid_move += signed_mid;
    s.sum_net_spread_move += net_spread;
    s.sum_net_friction_move += net_friction;
    if (is_non_overlapping) {
      ++s.non_overlapping;
      if (signed_mid > 0.0) {
        ++s.non_overlapping_hits;
      }
      if (net_friction > 0.0) {
        ++s.non_overlapping_net_hits;
      }
      const double delta = net_spread - s.non_overlapping_mean_spread;
      s.non_overlapping_mean_spread += delta / static_cast<double>(s.non_overlapping);
      const double delta2 = net_spread - s.non_overlapping_mean_spread;
      s.non_overlapping_m2_spread += delta * delta2;

      const double delta_net = net_friction - s.non_overlapping_mean_net;
      s.non_overlapping_mean_net += delta_net / static_cast<double>(s.non_overlapping);
      const double delta2_net = net_friction - s.non_overlapping_mean_net;
      s.non_overlapping_m2_net += delta_net * delta2_net;
    }
  };

  const bool use_time_horizon = config_.eval_horizon_ns > 0;
  while (!pending_signal_evals_.empty() &&
         (use_time_horizon ? (current_time_ns >= pending_signal_evals_.front().target_time_ns)
                           : (pending_signal_evals_.front().target_event_index <=
                              current_event_index))) {
    const PendingSignalEval item = pending_signal_evals_.front();
    pending_signal_evals_.pop_front();

    const double signed_mid = item.direction * (mid - item.entry_mid);
    const double net_spread =
        item.direction >= 0.0 ? (bid - item.entry_ask) : (item.entry_bid - ask);
    const double net_friction = net_spread - round_trip_friction;

    if (item.is_baseline) {
      accumulate(stats_.forward_baseline, signed_mid, net_spread, item.non_overlapping_kind);
    } else {
      accumulate(stats_.forward_all, signed_mid, net_spread, item.non_overlapping_all);
      if (item.kind == prediction::SignalKind::Continuation) {
        accumulate(stats_.forward_continuation, signed_mid, net_spread,
                   item.non_overlapping_kind);
      } else if (item.kind == prediction::SignalKind::Reversal) {
        accumulate(stats_.forward_reversal, signed_mid, net_spread, item.non_overlapping_kind);
      }

      // Paired comparison vs Naive Directional Baseline on identical non-overlapping blocks
      if (item.non_overlapping_all && item.baseline_direction != 0.0) {
        const double base_net_spread = item.baseline_direction >= 0.0
                                           ? (bid - item.entry_ask)
                                           : (item.entry_bid - ask);
        const double base_net_friction = base_net_spread - round_trip_friction;
        const double diff = net_friction - base_net_friction;
        auto& s = stats_.forward_all;
        ++s.paired_blocks;
        if (diff > 0.0) {
          ++s.paired_wins;
        }
        const double delta = diff - s.paired_diff_mean;
        s.paired_diff_mean += delta / static_cast<double>(s.paired_blocks);
        const double delta2 = diff - s.paired_diff_mean;
        s.paired_diff_m2 += delta * delta2;
      }

      const double clamped_score = std::clamp(item.score, 0.0, 1.0);
      const double hit_indicator = signed_mid > 0.0 ? 1.0 : 0.0;
      const double brier = (clamped_score - hit_indicator) * (clamped_score - hit_indicator);
      const auto update_cal = [&](CalibrationBin& b) {
        ++b.count;
        if (signed_mid > 0.0) {
          ++b.hits;
        }
        b.sum_score += clamped_score;
        b.sum_brier += brier;
        b.sum_net_spread_move += net_spread;
      };
      update_cal(stats_.calibration_total);
      const std::size_t bin_idx =
          std::min<std::size_t>(4, static_cast<std::size_t>(clamped_score * 5.0));
      update_cal(stats_.calibration_bins[bin_idx]);
    }
  }
}

ReplayStats ReplayEngine::run(std::istream& input, const std::string& source_name) {
  (void)source_name;  // surfaced by the caller/report, not needed by the loop
  stats_.initial_equity = config_.initial_capital;
  market::read_ticks(
      input, std::chrono::duration_cast<std::chrono::nanoseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
                 .count(),
      [this](const market::ParsedLine& line) {
        ++stats_.lines_read;
        if (!line.ok) {
          ++stats_.events_malformed;
          ++stats_.invalid_reasons["Malformed"];
          risk_.record_data_point(false, has_stream_time_ ? stream_time_ns_ : 0);
          return;
        }
        ++stats_.events_parsed;

        market::MarketEvent event = line.event;

        // --- validation ---------------------------------------------------
        const market::ValidationResult verdict = validator_.validate(event, std::nullopt);
        event.validity = verdict.validity;
        if (verdict.stale_gap) {
          event.flags |= market::EventFlags::StaleGap;
        }
        if (!verdict.ok()) {
          ++stats_.events_rejected;
          ++stats_.invalid_reasons[market::to_string(verdict.validity)];
          risk_.record_data_point(false, has_stream_time_ ? stream_time_ns_ : 0);
          return;
        }
        ++stats_.events_valid;
        if (market::has_flag(event.flags, market::EventFlags::StaleGap)) {
          ++stats_.stale_gaps;
        }
        stream_time_ns_ = event.source_time_ns;
        has_stream_time_ = true;
        if (stats_.first_event_time_ns == 0) {
          stats_.first_event_time_ns = event.source_time_ns;
        }
        stats_.last_event_time_ns = event.source_time_ns;
        risk_.record_data_point(true, event.source_time_ns);
        risk_.on_new_day(event.source_time_ns / kNanosPerDay);

        // --- 1. market data drives already-pending orders -----------------
        broker_.on_market(event);
        handle_order_events(event.source_time_ns, event.bid, event.ask);

        // --- 1b. position lifecycle: SL / TP / horizon exit ---------------
        bool lifecycle_exit_triggered = false;
        if (local_account_.position != 0.0) {
          ++position_held_events_;
          if (count_open_orders(broker_.orders()) == 0) {
            const double bid_p = market::price_to_double(event.bid);
            const double ask_p = market::price_to_double(event.ask);
            const double move_per_unit = local_account_.position > 0.0
                                             ? (bid_p - local_account_.avg_price)
                                             : (local_account_.avg_price - ask_p);
            const bool sl_hit =
                config_.stop_loss_price > 0.0 && move_per_unit <= -config_.stop_loss_price;
            const bool tp_hit =
                config_.take_profit_price > 0.0 && move_per_unit >= config_.take_profit_price;
            const bool horizon_events_hit =
                config_.hold_horizon_events > 0 &&
                position_held_events_ >= config_.hold_horizon_events;
            const bool horizon_time_hit =
                config_.hold_horizon_ns > 0 && position_opened_time_ns_ > 0 &&
                (event.source_time_ns - position_opened_time_ns_) >= config_.hold_horizon_ns;
            const bool horizon_hit = horizon_events_hit || horizon_time_hit;

            if (sl_hit || tp_hit || horizon_hit) {
              ++request_counter_;
              execution::OrderRequest exit_req;
              exit_req.client_order_id = "EXIT-" + std::to_string(stats_.events_valid) + "-" +
                                         std::to_string(request_counter_);
              exit_req.symbol = event.symbol;
              exit_req.side = local_account_.position > 0.0 ? execution::Side::Sell
                                                            : execution::Side::Buy;
              exit_req.units = std::fabs(local_account_.position);
              exit_req.type = execution::OrderType::Market;
              exit_req.created_ns = event.source_time_ns;

              if (sl_hit) {
                ++stats_.stop_loss_exits_submitted;
              } else if (tp_hit) {
                ++stats_.take_profit_exits_submitted;
              } else {
                ++stats_.horizon_exits_submitted;
              }
              ++stats_.orders_submitted;
              const auto submitted = broker_.submit(exit_req, event.source_time_ns);
              if (submitted.has_value()) {
                ++stats_.orders_accepted;
                submit_times_[submitted->order_id] = event.source_time_ns;
              }
              handle_order_events(event.source_time_ns, event.bid, event.ask);
              position_held_events_ = 0;
              lifecycle_exit_triggered = true;
            }
          }
        } else {
          position_held_events_ = 0;
        }

        // --- 2. behavioral features ---------------------------------------
        {
          telemetry::Stopwatch timer;
          behavior_.update(event);
          event_latency_.record(timer.elapsed_ns());
        }
        const behavior::BehaviorFeatures& features = behavior_.features();
        evaluate_matured_signals(features.event_index, event.source_time_ns, features.mid,
                                 market::price_to_double(event.bid),
                                 market::price_to_double(event.ask));

        // --- 3. prediction -------------------------------------------------
        prediction::Prediction prediction;
        {
          telemetry::Stopwatch timer;
          prediction = predictor_.predict(features);
          signal_latency_.record(timer.elapsed_ns());
        }

        if (config_.retain_tick_records) {
          SignalRecord signal_record;
          signal_record.time_ns = event.source_time_ns;
          signal_record.event_index = prediction.event_index;
          signal_record.kind = prediction.kind;
          signal_record.rejection = prediction.rejection;
          signal_record.valid = prediction.valid;
          signal_record.exhaustion = prediction.exhaustion;
          signal_record.continuation_score = prediction.continuation_score;
          signal_record.reversal_score = prediction.reversal_score;
          signal_record.confidence = prediction.confidence;
          signal_record.expected_move_log = prediction.expected_move_log;
          signal_record.direction = prediction.direction;
          signal_record.z_short = features.z_short;
          signal_record.persistence = features.persistence;
          signal_record.volatility = features.volatility;
          signal_record.quality_flags = behavior::describe(features.flags);
          signal_records_.push_back(signal_record);
        } else {
          hash_u64(signal_digest_, static_cast<std::uint64_t>(event.source_time_ns));
          hash_u64(signal_digest_, static_cast<std::uint64_t>(static_cast<int>(prediction.kind)));
          hash_u64(signal_digest_,
                   static_cast<std::uint64_t>(static_cast<int>(prediction.rejection)));
          signal_digest_ = hash_double(signal_digest_, prediction.exhaustion);
          signal_digest_ = hash_double(signal_digest_, prediction.expected_move_log);
        }

        ++stats_.predictions_total;
        ++stats_.signal_kinds[ prediction::to_string(prediction.kind)];
        ++stats_.rejection_reasons[prediction::to_string(prediction.rejection)];
        const std::size_t eval_h =
            config_.prediction.horizon_events > 0 ? config_.prediction.horizon_events : 1;
        const bool use_time_h = config_.eval_horizon_ns > 0;
        const std::uint64_t target_ev = features.event_index + eval_h;
        const std::int64_t target_ns = event.source_time_ns + config_.eval_horizon_ns;

        if (features.valid && features.disp_short != 0.0) {
          const bool no_base =
              use_time_h ? (event.source_time_ns >= next_non_overlap_time_base_)
                         : (features.event_index >= next_non_overlap_event_base_);
          if (no_base) {
            next_non_overlap_event_base_ = target_ev;
            next_non_overlap_time_base_ = target_ns;
          }
          pending_signal_evals_.push_back(PendingSignalEval{
              target_ev,
              target_ns,
              prediction::SignalKind::Continuation,
              features.disp_short > 0.0 ? 1.0 : -1.0,
              features.mid,
              market::price_to_double(event.bid),
              market::price_to_double(event.ask),
              true,
              false,
              no_base,
          });
        }

        if (prediction.has_signal()) {
          ++stats_.signals;
          ++stats_.predictions_usable;
          const bool no_all =
              use_time_h ? (event.source_time_ns >= next_non_overlap_time_all_)
                         : (features.event_index >= next_non_overlap_event_all_);
          if (no_all) {
            next_non_overlap_event_all_ = target_ev;
            next_non_overlap_time_all_ = target_ns;
          }
          bool no_kind = false;
          if (prediction.kind == prediction::SignalKind::Continuation) {
            no_kind = use_time_h ? (event.source_time_ns >= next_non_overlap_time_cont_)
                                 : (features.event_index >= next_non_overlap_event_cont_);
            if (no_kind) {
              next_non_overlap_event_cont_ = target_ev;
              next_non_overlap_time_cont_ = target_ns;
            }
          } else if (prediction.kind == prediction::SignalKind::Reversal) {
            no_kind = use_time_h ? (event.source_time_ns >= next_non_overlap_time_rev_)
                                 : (features.event_index >= next_non_overlap_event_rev_);
            if (no_kind) {
              next_non_overlap_event_rev_ = target_ev;
              next_non_overlap_time_rev_ = target_ns;
            }
          }
          const double active_score =
              prediction.kind == prediction::SignalKind::Reversal
                  ? prediction.reversal_score
                  : prediction.continuation_score;
          pending_signal_evals_.push_back(PendingSignalEval{
              target_ev,
              target_ns,
              prediction.kind,
              prediction.direction >= 0.0 ? 1.0 : -1.0,
              features.mid,
              market::price_to_double(event.bid),
              market::price_to_double(event.ask),
              false,
              no_all,
              no_kind,
              active_score,
              features.disp_short > 0.0 ? 1.0 : (features.disp_short < 0.0 ? -1.0 : 0.0),
          });
        }

        // --- 4. decision ---------------------------------------------------
        risk::Decision decision;
        {
          telemetry::Stopwatch timer;
          decision = risk_.evaluate(prediction, event, event.source_time_ns);
          decision_latency_.record(timer.elapsed_ns());
        }

        if (config_.retain_tick_records) {
          DecisionRecord decision_record;
          decision_record.time_ns = event.source_time_ns;
          decision_record.action = decision.action;
          decision_record.reason = decision.reason;
          decision_record.signal = decision.signal;
          decision_record.edge = decision.edge;
          decision_record.cost = decision.cost;
          decision_record.expected_move = decision.expected_move;
          decision_record.size_units = decision.size_units;
          decision_records_.push_back(decision_record);
        } else {
          hash_u64(signal_digest_, static_cast<std::uint64_t>(static_cast<int>(decision.action)));
          hash_u64(signal_digest_, static_cast<std::uint64_t>(static_cast<int>(decision.reason)));
          signal_digest_ = hash_double(signal_digest_, decision.edge);
          signal_digest_ = hash_double(signal_digest_, decision.size_units);
        }

        if (decision.allowed()) {
          ++stats_.decisions_allowed;
          if (config_.stream_decisions) {
            std::printf(
                "{\"event\":\"DECISION\",\"time_ns\":%lld,\"signal\":\"%s\","
                "\"direction\":%.0f,\"edge\":%.6f,\"cost\":%.6f,\"units\":%.2f}\n",
                static_cast<long long>(event.source_time_ns),
                prediction::to_string(decision.signal), prediction.direction,
                decision.edge, decision.cost, decision.size_units);
            std::fflush(stdout);
          }
        } else {
          ++stats_.decisions_rejected;
          ++stats_.decision_reasons[risk::to_string(decision.reason)];
        }

        // --- 5. execution --------------------------------------------------
        if (decision.allowed() && !lifecycle_exit_triggered) {
          ++request_counter_;
          execution::OrderRequest request;
          request.client_order_id =
              "SIG-" + std::to_string(prediction.event_index) + "-" +
              std::to_string(request_counter_);
          request.symbol = event.symbol;
          request.side =
              prediction.direction >= 0.0 ? execution::Side::Buy : execution::Side::Sell;
          request.units = decision.size_units;
          request.type = execution::OrderType::Market;
          request.created_ns = event.source_time_ns;

          ++stats_.orders_submitted;
          const auto submitted = broker_.submit(request, event.source_time_ns);
          if (submitted.has_value()) {
            ++stats_.orders_accepted;
            submit_times_[submitted->order_id] = event.source_time_ns;
            risk_.record_order_placed();
          }
          handle_order_events(event.source_time_ns, event.bid, event.ask);
        }

        // --- 6. mark to market --------------------------------------------
        update_equity(event.bid, event.ask, event.source_time_ns);
      });

  finalize();
  return stats_;
}

void ReplayEngine::finalize() {
  if (finalized_) {
    return;
  }
  finalized_ = true;

  stats_.realized_pnl = local_account_.realized_pnl;
  stats_.commission_total = commission_paid_;
  stats_.slippage_total = slippage_paid_;

  stats_.event_latency = event_latency_.stats();
  stats_.signal_latency = signal_latency_.stats();
  stats_.decision_latency = decision_latency_.stats();
  stats_.order_latency = order_latency_.stats();

  stats_.reconciliation = execution::reconcile(local_account_, broker_.account(), 1e-6);

  // Deterministic fingerprint of the whole run (used by replay tests).
  std::uint64_t h = config_.retain_tick_records ? 14695981039346656037ULL : signal_digest_;
  if (config_.retain_tick_records) {
    for (const auto& record : signal_records_) {
      hash_u64(h, static_cast<std::uint64_t>(record.time_ns));
      hash_u64(h, static_cast<std::uint64_t>(static_cast<int>(record.kind)));
      hash_u64(h, static_cast<std::uint64_t>(static_cast<int>(record.rejection)));
      h = hash_double(h, record.exhaustion);
      h = hash_double(h, record.expected_move_log);
    }
    for (const auto& record : decision_records_) {
      hash_u64(h, static_cast<std::uint64_t>(static_cast<int>(record.action)));
      hash_u64(h, static_cast<std::uint64_t>(static_cast<int>(record.reason)));
      h = hash_double(h, record.edge);
      h = hash_double(h, record.size_units);
    }
  }
  for (const auto& fill : broker_.fills()) {
    hash_u64(h, static_cast<std::uint64_t>(fill.time_ns));
    h = hash_double(h, fill.units);
    h = hash_double(h, fill.price);
  }
  h = hash_double(h, stats_.net_pnl);
  h = hash_double(h, stats_.max_drawdown);
  stats_.digest = h;
}

}  // namespace vectra::backtest
