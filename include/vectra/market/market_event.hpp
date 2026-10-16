// Normalized market event types.
//
// Prices are stored as fixed-point integers (scale 1e-7) so that equality and
// ordering comparisons never suffer from floating-point rounding. Doubles are
// only introduced when a mathematical transformation (log return) requires it.
#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vectra::market {

using Price = std::int64_t;  // fixed-point price, value = price / kPriceScale

inline constexpr Price kPriceScale = 10'000'000;  // 1e-7 resolution
inline constexpr int kPriceDecimals = 7;

// Result of validation for a single event. Anything other than Ok means the
// event must not be fed into the behavioral engine.
enum class Validity {
  Ok = 0,
  Malformed,          // unparsable or missing required fields
  WrongSymbol,        // symbol does not match the configured instrument
  NonPositivePrice,   // bid/ask missing, zero or negative
  NegativeSpread,     // ask < bid
  WideSpread,         // spread larger than the configured maximum
  Duplicate,          // same sequence number or same source timestamp as before
  OutOfOrder,         // timestamp/sequence moved backwards beyond tolerance
  Stale,              // timestamp lags the stream clock beyond the threshold
  FutureTimestamp,    // timestamp ahead of the reference clock beyond tolerance
};

const char* to_string(Validity v);

// Non-fatal annotations. The event stays usable but downstream scoring must
// treat the data with extra caution.
enum class EventFlags : unsigned {
  None      = 0,
  StaleGap  = 1u << 0,  // forward gap since previous event exceeds the threshold
};

inline EventFlags operator|(EventFlags a, EventFlags b) {
  return static_cast<EventFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline EventFlags& operator|=(EventFlags& a, EventFlags b) {
  a = a | b;
  return a;
}
inline bool has_flag(EventFlags f, EventFlags bit) {
  return (static_cast<unsigned>(f) & static_cast<unsigned>(bit)) != 0;
}

struct MarketEvent {
  std::string symbol;
  Price bid = 0;  // fixed-point
  Price ask = 0;  // fixed-point

  bool has_source_time = false;
  std::int64_t source_time_ns = 0;  // provider clock (own domain)

  // Local receive time read from std::chrono::steady_clock at ingestion.
  // It is only ever used for duration measurements; it is NEVER subtracted
  // from source_time_ns (unrelated clock domains).
  std::int64_t local_receive_ns = 0;

  std::optional<std::uint64_t> sequence;

  Validity validity = Validity::Ok;
  EventFlags flags = EventFlags::None;
  std::string note;  // parse/validation detail, empty when clean

  Price spread() const { return ask - bid; }

  // Exact integer midpoint (truncating half-units is irrelevant at 1e-7 scale
  // and keeps comparisons deterministic).
  Price mid_fixed() const { return bid + (ask - bid) / 2; }

  // Double midpoint used for log-return arithmetic only.
  double mid_price() const {
    return static_cast<double>(bid + (ask - bid) / 2) / static_cast<double>(kPriceScale);
  }
};

// Parses a decimal string such as "1.10010" into fixed-point.
// Accepts at most kPriceDecimals fractional digits; anything else is rejected
// as malformed rather than silently rounded.
bool parse_price(std::string_view text, Price& out);

// Renders a fixed-point price with at least 5 and at most kPriceDecimals
// fractional digits (trailing zeros trimmed).
std::string format_price(Price price);

inline double price_to_double(Price p) {
  return static_cast<double>(p) / static_cast<double>(kPriceScale);
}

inline Price double_to_price(double v) {
  return static_cast<Price>(v * static_cast<double>(kPriceScale) + (v >= 0 ? 0.5 : -0.5));
}

}  // namespace vectra::market
