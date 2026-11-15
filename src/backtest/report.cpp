// Markdown replay report writer.
#include "vectra/backtest/report.hpp"

#include <cstdio>
#include <ostream>

namespace vectra::backtest {

namespace {

std::string money(double v) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.2f", v);
  return std::string(buffer);
}

std::string number(double v, int decimals = 6) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
  return std::string(buffer);
}

std::string percent(double v) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.3f%%", v);
  return std::string(buffer);
}

// Latency is reported in microseconds: event-loop latencies here are
// sub-millisecond and nanosecond digits would be noise.
std::string micros(double ns) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.1f", ns / 1000.0);
  return std::string(buffer);
}

void write_quantile_row(std::ostream& out, const char* label,
                        const telemetry::QuantileStats& stats) {
  out << "| " << label << " | ";
  if (!stats.available) {
    out << "n/a (only " << stats.count << " samples, need " << stats.min_samples_required
        << ") | n/a | n/a | " << stats.count << " |\n";
    return;
  }
  out << micros(stats.median_ns) << " | " << micros(stats.p95_ns) << " | "
      << micros(stats.p99_ns) << " | " << stats.count << " |\n";
}

void write_breakdown(std::ostream& out, const std::map<std::string, int>& counts) {
  bool first = true;
  for (const auto& [key, value] : counts) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << key << "=" << value;
  }
  if (first) {
    out << "none";
  }
}

std::string format_duration_ns(std::int64_t ns) {
  if (ns <= 0) {
    return "0";
  }
  const double seconds = static_cast<double>(ns) / 1e9;
  if (seconds >= 86400.0) {
    return number(seconds / 86400.0, 2) + " days";
  }
  if (seconds >= 3600.0) {
    return number(seconds / 3600.0, 2) + " hours";
  }
  if (seconds >= 60.0) {
    return number(seconds / 60.0, 2) + " minutes";
  }
  return number(seconds, 2) + " s";
}

}  // namespace

void write_markdown_report(std::ostream& out, const ReplayEngine& engine,
                           const std::string& source_name) {
  const ReplayStats& s = engine.stats();
  const ReplayConfig& cfg = engine.config();

  out << "# VECTRA replay report\n\n";
  out << "Source: `" << source_name << "`\n\n";
  out << "> Experimental research engine. Scores in this report are **not** "
         "calibrated probabilities, and nothing here is a claim of "
         "profitability. Synthetic data cannot demonstrate a trading edge.\n\n";

  // ---------------------------------------------------------------- events
  out << "## 1. Market data\n\n";
  out << "| Metric | Value |\n|---|---|\n";
  out << "| Lines read | " << s.lines_read << " |\n";
  out << "| Rows parsed into events | " << s.events_parsed << " |\n";
  out << "| Valid events | " << s.events_valid << " |\n";
  out << "| Rejected events | " << s.events_rejected << " |\n";
  out << "| Malformed rows | " << s.events_malformed << " |\n";
  out << "| Stale gaps flagged | " << s.stale_gaps << " |\n";
  out << "| Rejection reasons | ";
  write_breakdown(out, s.invalid_reasons);
  out << " |\n";
  if (s.first_event_time_ns > 0 && s.last_event_time_ns > s.first_event_time_ns) {
    out << "| Stream duration | "
        << format_duration_ns(s.last_event_time_ns - s.first_event_time_ns) << " |\n";
  }
  out << "\n";

  // ------------------------------------------------------------ predictions
  out << "## 2. Predictions and signal distribution\n\n";
  out << "| Metric | Value |\n|---|---|\n";
  out << "| Event-level predictions | " << s.predictions_total << " |\n";
  out << "| Usable signals (valid, non-neutral) | " << s.signals << " |\n";
  out << "| Kind distribution | ";
  write_breakdown(out, s.signal_kinds);
  out << " |\n";
  out << "| Rejection reasons | ";
  write_breakdown(out, s.rejection_reasons);
  out << " |\n";
  if (s.predictions_total > 0) {
    out << "| Signal rate | "
        << percent(100.0 * static_cast<double>(s.signals) /
                   static_cast<double>(s.predictions_total))
        << " |\n";
  }
  out << "\n";
  if (s.forward_all.evaluated > 0) {
    out << "### Causal forward-return evaluation (horizon = " << cfg.prediction.horizon_events
        << " valid events)\n\n";
    out << "| Signal kind | Evaluated | Hit rate (mid) | Mean signed mid move | Mean spread-crossed move |\n"
           "|---|---|---|---|---|\n";
    const auto write_fwd_row = [&out](const char* label, const SignalHorizonStats& fs) {
      out << "| " << label << " | " << fs.evaluated << " | ";
      if (fs.evaluated == 0) {
        out << "n/a | n/a | n/a |\n";
        return;
      }
      out << percent(100.0 * fs.hit_rate()) << " | "
          << number(fs.mean_signed_mid_move(), 6) << " | "
          << number(fs.mean_net_spread_move(), 6) << " |\n";
    };
    write_fwd_row("Continuation", s.forward_continuation);
    write_fwd_row("Reversal", s.forward_reversal);
    write_fwd_row("All usable signals", s.forward_all);
    write_fwd_row("Naive directional baseline", s.forward_baseline);
    out << "\n";
    out << "#### Autocorrelation-robust non-overlapping horizon blocks\n\n";
    out << "| Signal kind | Independent blocks | Block hit rate (mid) | Block mean spread-crossed move | Student t-stat |\n"
           "|---|---|---|---|---|\n";
    const auto write_block_row = [&out](const char* label, const SignalHorizonStats& fs) {
      out << "| " << label << " | " << fs.non_overlapping << " | ";
      if (fs.non_overlapping == 0) {
        out << "n/a | n/a | n/a |\n";
        return;
      }
      out << percent(100.0 * fs.non_overlapping_hit_rate()) << " | "
          << number(fs.non_overlapping_mean_spread, 6) << " | ";
      if (fs.non_overlapping < 2) {
        out << "n/a |\n";
      } else {
        out << number(fs.non_overlapping_t_stat(), 2) << " |\n";
      }
    };
    write_block_row("Continuation", s.forward_continuation);
    write_block_row("Reversal", s.forward_reversal);
    write_block_row("All usable signals", s.forward_all);
    write_block_row("Naive directional baseline", s.forward_baseline);
    out << "\n";

    if (s.calibration_total.count > 0) {
      out << "#### Score reliability & Brier calibration (usable signals)\n\n";
      out << "| Score bin | Evaluated | Mean score | Realized hit rate (mid) | Mean spread-crossed move | Brier score |\n"
             "|---|---|---|---|---|---|\n";
      static constexpr const char* kBinLabels[5] = {
          "[0.0, 0.2)", "[0.2, 0.4)", "[0.4, 0.6)", "[0.6, 0.8)", "[0.8, 1.0]"};
      const auto write_cal_row = [&out](const char* label, const CalibrationBin& b) {
        if (b.count == 0) {
          return;
        }
        out << "| " << label << " | " << b.count << " | "
            << number(b.mean_score(), 4) << " | "
            << percent(100.0 * b.hit_rate()) << " | "
            << number(b.mean_net_spread_move(), 6) << " | "
            << number(b.brier_score(), 4) << " |\n";
      };
      for (std::size_t i = 0; i < 5; ++i) {
        write_cal_row(kBinLabels[i], s.calibration_bins[i]);
      }
      write_cal_row("Total", s.calibration_total);
      out << "\n";
    }
  }
  out << "Scores are continuity/reversal **scores** in [0,1], produced by a "
         "fixed formula with uncalibrated thresholds (see "
         "docs/mathematical_model.md). They have not been calibrated against "
         "realized outcomes and must not be read as probabilities.\n\n";

  // ------------------------------------------------------------- decisions
  out << "## 3. Decisions and risk\n\n";
  out << "| Metric | Value |\n|---|---|\n";
  out << "| Decisions allowed | " << s.decisions_allowed << " |\n";
  out << "| Decisions rejected | " << s.decisions_rejected << " |\n";
  out << "| Rejection reasons | ";
  write_breakdown(out, s.decision_reasons);
  out << " |\n";
  out << "| Kill switch | " << (cfg.risk.kill_switch ? "ACTIVE" : "inactive") << " |\n";
  out << "| Min expected edge (price units) | " << number(cfg.risk.min_expected_edge, 6)
      << " |\n";
  out << "| Max position (base units) | " << money(cfg.risk.max_position_units) << " |\n";
  out << "| Max daily loss (quote) | " << money(cfg.risk.max_daily_loss) << " |\n";
  out << "| Max open orders | " << cfg.risk.max_open_orders << " |\n";
  out << "\n";

  // ------------------------------------------------------------- execution
  out << "## 4. Execution (simulated)\n\n";
  out << "| Metric | Value |\n|---|---|\n";
  out << "| Order submissions | " << s.orders_submitted << " |\n";
  out << "| Accepted by gateway | " << s.orders_accepted << " |\n";
  out << "| Venue rejections (simulated) | " << s.orders_venue_rejected << " |\n";
  out << "| Cancellations | " << s.orders_cancelled << " |\n";
  out << "| Duplicate submissions blocked | " << s.orders_duplicate << " |\n";
  out << "| Fill events | " << s.fills << " |\n";
  out << "| of which partial fills | " << s.partial_fills << " |\n";
  if (s.horizon_exits_submitted > 0) {
    out << "| Horizon exits submitted | " << s.horizon_exits_submitted << " |\n";
  }
  if (s.stop_loss_exits_submitted > 0) {
    out << "| Stop-loss exits submitted | " << s.stop_loss_exits_submitted << " |\n";
  }
  if (s.take_profit_exits_submitted > 0) {
    out << "| Take-profit exits submitted | " << s.take_profit_exits_submitted << " |\n";
  }
  out << "| Reconciliation | "
      << (s.reconciliation.matched ? "matched" : "MISMATCH")
      << " (" << s.reconciliation.detail << ") |\n";
  out << "\nExecution model: fills occur against the next quote after the "
         "configured latency, buys lift the ask and sells hit the bid, with "
         "configurable slippage and commission. No order fills at the "
         "midpoint by default.\n\n";

  // ----------------------------------------------------------- performance
  out << "## 5. Simulated performance\n\n";
  out << "| Metric | Value |\n|---|---|\n";
  out << "| Initial equity | " << money(s.initial_equity) << " |\n";
  out << "| Final equity | " << money(s.final_equity) << " |\n";
  out << "| Realized P&L | " << money(s.realized_pnl) << " |\n";
  out << "| Gross P&L (after spread, before commission/slippage) | "
      << money(s.gross_pnl) << " |\n";
  out << "| Net P&L | " << money(s.net_pnl) << " |\n";
  out << "| Commission paid | " << money(s.commission_total) << " |\n";
  out << "| Slippage paid | " << money(s.slippage_total) << " |\n";
  out << "| Estimated spread cost (informational) | " << money(s.spread_cost_total)
      << " |\n";
  out << "| Max drawdown | " << money(s.max_drawdown) << " (" << percent(s.max_drawdown_pct)
      << ") |\n";
  out << "\nThese results come from a synthetic replay. They are a **pipeline "
         "smoke test**, not evidence of an edge.\n\n";

  // -------------------------------------------------------------- latency
  out << "## 6. Latency (microseconds)\n\n";
  out << "| Stage | Median | p95 | p99 | Samples |\n|---|---|---|---|---|\n";
  write_quantile_row(out, "Event processing", s.event_latency);
  write_quantile_row(out, "Signal calculation", s.signal_latency);
  write_quantile_row(out, "Decision", s.decision_latency);
  write_quantile_row(out, "Simulated order (submit to ack, venue clock)", s.order_latency);
  out << "\nThe first three rows are local processing durations measured with "
         "std::chrono::steady_clock. The simulated-order row is measured on the "
         "venue's simulated clock (submit to acknowledgement) and, being driven "
         "by market-event timestamps, is quantized by the tick interval. Provider "
         "timestamps are never subtracted from local timestamps: no clock "
         "synchronization model exists in v0.1.\n\n";

  // ----------------------------------------------------------- limitations
  out << "## 7. Limitations and assumptions\n\n";
  out << "- **Synthetic or limited data**: this run does not demonstrate a "
         "trading edge on real markets. In-sample threshold tuning would be "
         "look-ahead in spirit even where it is not in code.\n";
  out << "- **No order flow**: tick updates are quotes only. There is no "
         "traded volume, no order book, no aggressor side, and no net "
         "positioning data. Tick counts are not volume.\n";
  out << "- **No behavioral attribution**: the engine never labels a move as "
         "forced liquidation, stop cascades or retail activity; those are "
         "latent hypotheses it cannot observe.\n";
  out << "- **Simulated venue**: no queue position, no market impact, no "
         "depth, no partial-fill selection bias, no rejects beyond the "
         "configured model, no swaps/rollover, no margin or financing costs.\n";
  out << "- **Uncalibrated scores**: continuation/reversal/confidence values "
         "are scores from a fixed formula with configurable, uncalibrated "
         "thresholds (marked UNCALIBRATED in code and config).\n";
  out << "- **Event-counted horizons**: windows are measured in ticks, not "
         "wall-clock time, so results depend on the tick rate of the source.\n";
  out << "- **Single instrument**: v0.1 replays one symbol per run; "
         "cross-asset and portfolio effects are untested.\n";
  out << "- **Split discipline**: when real data arrives, separate "
         "development / validation / out-of-sample periods and never tune "
         "thresholds on the final test period.\n\n";

  out << "---\nRun digest (FNV-1a over signals, decisions, fills, P&L): `"
      << std::hex << s.digest << std::dec << "`\n";
}

}  // namespace vectra::backtest
