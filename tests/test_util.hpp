// Shared helpers for the VECTRA test suite.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vectra/market/market_event.hpp"

namespace vectra::test {

inline market::MarketEvent make_event(double bid, double ask, std::int64_t ts_ns,
                                      std::optional<std::uint64_t> seq = std::nullopt,
                                      const std::string& symbol = "EURUSD") {
  market::MarketEvent event;
  event.symbol = symbol;
  event.bid = market::double_to_price(bid);
  event.ask = market::double_to_price(ask);
  event.source_time_ns = ts_ns;
  event.has_source_time = true;
  event.sequence = seq;
  return event;
}

// Mid-quote helper: symmetric spread around `mid`.
inline market::MarketEvent make_quote(double mid, double spread, std::int64_t ts_ns,
                                      std::optional<std::uint64_t> seq = std::nullopt) {
  return make_event(mid - spread * 0.5, mid + spread * 0.5, ts_ns, seq);
}

// Deterministic pseudo-random walk (LCG, no <random> so results are identical
// on every standard library implementation).
inline std::vector<market::MarketEvent> make_walk(std::size_t count, double start_mid,
                                                  double spread, double vol, double drift,
                                                  std::int64_t start_ts_ns = 0,
                                                  std::int64_t step_ns = 100'000'000) {
  std::vector<market::MarketEvent> events;
  events.reserve(count);
  std::uint64_t state = 0x2545F4914F6CDD1DULL;
  double mid = start_mid;
  for (std::size_t i = 0; i < count; ++i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    const double unit =
        static_cast<double>((state >> 11) & 0x1FFFFFFFFFFFFFULL) / 9007199254740992.0;
    // Approximate normal draw in roughly [-1.7, 1.7].
    const double gauss = (unit - 0.5) * 3.4641016151377544;
    mid += drift + vol * gauss;
    events.push_back(make_quote(mid, spread, start_ts_ns + static_cast<std::int64_t>(i) * step_ns,
                                static_cast<std::uint64_t>(i + 1)));
  }
  return events;
}

// Renders a vector of events as CSV text (for replay tests).
inline std::string to_csv(const std::vector<market::MarketEvent>& events,
                          bool with_header = true) {
  std::string out;
  if (with_header) {
    out += "symbol,bid,ask,source_ts_ns,sequence\n";
  }
  for (const auto& event : events) {
    out += event.symbol;
    out += ",";
    out += market::format_price(event.bid);
    out += ",";
    out += market::format_price(event.ask);
    out += ",";
    out += std::to_string(event.source_time_ns);
    out += ",";
    out += event.sequence ? std::to_string(*event.sequence) : std::string("");
    out += "\n";
  }
  return out;
}

}  // namespace vectra::test
