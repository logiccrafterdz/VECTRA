// Event-driven replay engine implementation.
#include "vectra/backtest/replay_engine.hpp"

#include <algorithm>
#include <cmath>
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

  const double realized_before = local_account_.realized_pnl;
  apply_fill_to(local_account_, event.side, event.units, event.price, commission);
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

        // --- 2. behavioral features ---------------------------------------
        {
          telemetry::Stopwatch timer;
          behavior_.update(event);
          event_latency_.record(timer.elapsed_ns());
        }
        const behavior::BehaviorFeatures& features = behavior_.features();

        // --- 3. prediction -------------------------------------------------
        prediction::Prediction prediction;
        {
          telemetry::Stopwatch timer;
          prediction = predictor_.predict(features);
          signal_latency_.record(timer.elapsed_ns());
        }

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

        ++stats_.predictions_total;
        ++stats_.signal_kinds[ prediction::to_string(prediction.kind)];
        ++stats_.rejection_reasons[prediction::to_string(prediction.rejection)];
        if (prediction.has_signal()) {
          ++stats_.signals;
          ++stats_.predictions_usable;
        }

        // --- 4. decision ---------------------------------------------------
        risk::Decision decision;
        {
          telemetry::Stopwatch timer;
          decision = risk_.evaluate(prediction, event, event.source_time_ns);
          decision_latency_.record(timer.elapsed_ns());
        }

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

        if (decision.allowed()) {
          ++stats_.decisions_allowed;
        } else {
          ++stats_.decisions_rejected;
          ++stats_.decision_reasons[risk::to_string(decision.reason)];
        }

        // --- 5. execution --------------------------------------------------
        if (decision.allowed()) {
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
  std::uint64_t h = 14695981039346656037ULL;
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
