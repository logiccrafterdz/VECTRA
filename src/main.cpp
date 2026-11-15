// VECTRA command line entry point: replay a tick CSV and emit a report.
//
// Usage:
//   vectra --config config/default.json --data examples/sample_ticks.csv
//          [--report out/report.md] [--quiet]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "vectra/backtest/replay_engine.hpp"
#include "vectra/backtest/report.hpp"
#include "vectra/config/settings.hpp"

namespace {

void print_usage() {
  std::printf(
      "VECTRA replay tool (v0.1)\n"
      "\n"
      "Usage:\n"
      "  vectra --data <ticks.csv|-> [--config <config.json>] [--report <out.md>] [--stream] [--quiet]\n"
      "\n"
      "Options:\n"
      "  --data <path>     CSV tick file, or '-' to read live stream from stdin\n"
      "  --config <path>   JSON configuration (default: config/default.json if present,\n"
      "                    otherwise built-in defaults)\n"
      "  --report <path>   write a markdown report to this path\n"
      "  --stream          emit real-time JSONL decision and fill events to stdout\n"
      "  --quiet           print only the one-line summary\n"
      "  --help            show this message\n");
}

struct Options {
  std::string data_path;
  std::string config_path;
  std::string report_path;
  bool stream = false;
  bool quiet = false;
};

bool parse_args(int argc, char** argv, Options& options, bool& show_help) {
  show_help = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto require_value = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", name);
        return nullptr;
      }
      return argv[++i];
    };
    if (arg == "--help" || arg == "-h") {
      show_help = true;
      return true;
    } else if (arg == "--quiet" || arg == "-q") {
      options.quiet = true;
    } else if (arg == "--stream") {
      options.stream = true;
      if (options.data_path.empty()) {
        options.data_path = "-";
      }
    } else if (arg == "--data") {
      const char* value = require_value("--data");
      if (value == nullptr) return false;
      options.data_path = value;
    } else if (arg == "--config") {
      const char* value = require_value("--config");
      if (value == nullptr) return false;
      options.config_path = value;
    } else if (arg == "--report") {
      const char* value = require_value("--report");
      if (value == nullptr) return false;
      options.report_path = value;
    } else {
      std::fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

bool file_exists(const std::string& path) {
  std::ifstream file(path);
  return static_cast<bool>(file);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  bool show_help = false;
  if (!parse_args(argc, argv, options, show_help)) {
    print_usage();
    return 2;
  }
  if (show_help) {
    print_usage();
    return 0;
  }
  if (options.data_path.empty()) {
    std::fprintf(stderr, "error: --data is required\n");
    print_usage();
    return 2;
  }

  // --- configuration -------------------------------------------------------
  vectra::config::Settings settings = vectra::config::default_settings();
  std::string config_path = options.config_path;
  if (config_path.empty() && file_exists("config/default.json")) {
    config_path = "config/default.json";
  }
  if (!config_path.empty()) {
    std::string error;
    if (!vectra::config::load_settings(config_path, &settings, &error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 2;
    }
  }

  if (options.stream) {
    settings.replay.stream_decisions = true;
  }

  // --- input & replay ------------------------------------------------------
  std::ifstream file_input;
  std::istream* in_stream = &std::cin;
  if (options.data_path != "-") {
    file_input.open(options.data_path);
    if (!file_input) {
      std::fprintf(stderr, "error: cannot open data file: %s\n", options.data_path.c_str());
      return 2;
    }
    in_stream = &file_input;
  }

  vectra::backtest::ReplayEngine engine(settings.replay);
  const vectra::backtest::ReplayStats stats = engine.run(*in_stream, options.data_path);
  if (file_input.is_open()) {
    file_input.close();
  }

  // --- report --------------------------------------------------------------
  if (!options.report_path.empty()) {
    std::ofstream report(options.report_path);
    if (!report) {
      std::fprintf(stderr, "error: cannot write report to %s\n", options.report_path.c_str());
      return 2;
    }
    vectra::backtest::write_markdown_report(report, engine, options.data_path);
  }

  // --- console summary -----------------------------------------------------
  if (!options.quiet) {
    std::printf("VECTRA replay complete\n");
    std::printf("  data:            %s\n", options.data_path.c_str());
    if (!config_path.empty()) {
      std::printf("  config:          %s\n", config_path.c_str());
    }
    std::printf("  events valid:    %zu (rejected %zu, malformed %zu)\n",
                stats.events_valid, stats.events_rejected, stats.events_malformed);
    std::printf("  predictions:     %zu, usable signals: %zu\n", stats.predictions_total,
                stats.signals);
    std::printf("  decisions:       %zu allowed, %zu rejected\n", stats.decisions_allowed,
                stats.decisions_rejected);
    std::printf("  orders:          %zu submitted, %zu fills\n", stats.orders_submitted,
                stats.fills);
    std::printf("  gross P&L:       %.2f   net P&L: %.2f\n", stats.gross_pnl, stats.net_pnl);
    std::printf("  max drawdown:    %.2f (%.3f%%)\n", stats.max_drawdown,
                stats.max_drawdown_pct);
    std::printf("  reconciliation:  %s\n",
                stats.reconciliation.matched ? "matched" : "MISMATCH");
    if (!options.report_path.empty()) {
      std::printf("  report:          %s\n", options.report_path.c_str());
    }
  } else {
    std::printf("valid=%zu signals=%zu allowed=%zu fills=%zu net=%.2f\n", stats.events_valid,
                stats.signals, stats.decisions_allowed, stats.fills, stats.net_pnl);
  }

  if (!stats.reconciliation.matched) {
    std::fprintf(stderr, "error: account reconciliation failed: %s\n",
                 stats.reconciliation.detail.c_str());
    return 1;
  }
  return 0;
}
