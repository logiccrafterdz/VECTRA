// Event validation: rejects or flags malformed, duplicated, out-of-order,
// stale and wide-spread quotes before they reach the behavioral engine.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "vectra/market/market_event.hpp"

namespace vectra::market {

struct ValidatorConfig {
  std::string expected_symbol;           // empty = accept any symbol
  Price max_spread = 50'000;             // 0.00500 in fixed-point units
  bool require_source_time = true;
  bool require_sequence = false;

  std::int64_t stale_threshold_ns = 5'000'000'000;     // 5 s lag => Stale
  std::int64_t reorder_tolerance_ns = 0;               // allowed backward jump
  std::int64_t stale_gap_threshold_ns = 5'000'000'000; // forward gap => flag
  std::int64_t future_skew_ns = 1'000'000'000;         // 1 s ahead => Future
};

struct ValidationResult {
  Validity validity = Validity::Ok;
  bool stale_gap = false;  // non-fatal forward-gap flag
  std::string note;

  bool ok() const { return validity == Validity::Ok; }
};

// Stateful validator: remembers the last accepted timestamp/sequence so that
// duplicates and ordering violations are detectable across events.
// A single instance must be used by a single stream (no threads).
class EventValidator {
 public:
  explicit EventValidator(ValidatorConfig config);

  // `reference_clock_ns` is an independent "now" (e.g. a live local clock).
  // For pure replay there is no independent clock, so pass std::nullopt and
  // ordering is judged only against previously accepted source timestamps.
  ValidationResult validate(const MarketEvent& event,
                            std::optional<std::int64_t> reference_clock_ns = std::nullopt);

  void reset();

  const ValidatorConfig& config() const { return config_; }
  std::size_t accepted() const { return accepted_; }
  std::size_t rejected() const { return rejected_; }

 private:
  ValidatorConfig config_;
  bool has_last_ = false;
  std::int64_t last_time_ns_ = 0;
  bool has_last_seq_ = false;
  std::uint64_t last_seq_ = 0;
  std::size_t accepted_ = 0;
  std::size_t rejected_ = 0;
};

}  // namespace vectra::market
