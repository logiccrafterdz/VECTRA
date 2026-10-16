// Event validator implementation.
#include "vectra/market/validator.hpp"

namespace vectra::market {

EventValidator::EventValidator(ValidatorConfig config) : config_(std::move(config)) {}

void EventValidator::reset() {
  has_last_ = false;
  last_time_ns_ = 0;
  has_last_seq_ = false;
  last_seq_ = 0;
  accepted_ = 0;
  rejected_ = 0;
}

namespace {

ValidationResult reject(Validity v, std::string note, bool* rejected) {
  *rejected = true;
  return ValidationResult{v, false, std::move(note)};
}

}  // namespace

ValidationResult EventValidator::validate(const MarketEvent& event,
                                          std::optional<std::int64_t> reference_clock_ns) {
  bool rejected = false;
  ValidationResult result;

  if (!config_.expected_symbol.empty() && event.symbol != config_.expected_symbol) {
    return reject(Validity::WrongSymbol, "symbol '" + event.symbol + "' != expected '" +
                                             config_.expected_symbol + "'",
                  &rejected);
  }

  if (event.bid <= 0 || event.ask <= 0) {
    return reject(Validity::NonPositivePrice,
                  "bid/ask must be strictly positive", &rejected);
  }
  if (event.ask < event.bid) {
    return reject(Validity::NegativeSpread,
                  "ask " + format_price(event.ask) + " < bid " + format_price(event.bid),
                  &rejected);
  }
  if (event.spread() > config_.max_spread) {
    return reject(Validity::WideSpread,
                  "spread " + format_price(event.spread()) + " > max " +
                      format_price(config_.max_spread),
                  &rejected);
  }
  if (config_.require_source_time && !event.has_source_time) {
    return reject(Validity::Malformed, "missing source timestamp", &rejected);
  }
  if (config_.require_sequence && !event.sequence.has_value()) {
    return reject(Validity::Malformed, "missing sequence number", &rejected);
  }

  // Independent-clock future check (live feeds). Not applicable in replay.
  if (reference_clock_ns.has_value() && event.has_source_time) {
    const std::int64_t ahead = event.source_time_ns - *reference_clock_ns;
    if (ahead > config_.future_skew_ns) {
      return reject(Validity::FutureTimestamp,
                    "timestamp is " + std::to_string(ahead) + " ns ahead of reference clock",
                    &rejected);
    }
  }

  if (has_last_ && event.has_source_time) {
    // Sequence ordering first: duplicates are a distinct class from ordering.
    if (event.sequence.has_value() && has_last_seq_) {
      if (*event.sequence == last_seq_) {
        return reject(Validity::Duplicate,
                      "sequence " + std::to_string(*event.sequence) + " already seen",
                      &rejected);
      }
      if (*event.sequence < last_seq_) {
        return reject(Validity::OutOfOrder,
                      "sequence " + std::to_string(*event.sequence) + " < last " +
                          std::to_string(last_seq_),
                      &rejected);
      }
    }

    if (event.source_time_ns < last_time_ns_) {
      const std::int64_t lag = last_time_ns_ - event.source_time_ns;
      if (lag > config_.stale_threshold_ns) {
        return reject(Validity::Stale,
                      "timestamp lags stream clock by " + std::to_string(lag) + " ns",
                      &rejected);
      }
      if (lag > config_.reorder_tolerance_ns) {
        return reject(Validity::OutOfOrder,
                      "timestamp moved back by " + std::to_string(lag) + " ns",
                      &rejected);
      }
      // Within reorder tolerance: accept and annotate.
      result.stale_gap = false;
    } else if (event.source_time_ns == last_time_ns_ && !event.sequence.has_value()) {
      return reject(Validity::Duplicate, "identical source timestamp without sequence",
                    &rejected);
    } else {
      const std::int64_t gap = event.source_time_ns - last_time_ns_;
      if (gap > config_.stale_gap_threshold_ns) {
        result.stale_gap = true;  // flagged, not rejected
        result.note = "forward gap of " + std::to_string(gap) + " ns";
      }
    }
  }

  if (rejected) {
    ++rejected_;
    return result;
  }

  // Commit stream state only for accepted events.
  if (event.has_source_time) {
    last_time_ns_ = event.source_time_ns;
    has_last_ = true;
  }
  if (event.sequence.has_value()) {
    last_seq_ = *event.sequence;
    has_last_seq_ = true;
  }
  ++accepted_;
  return result;
}

}  // namespace vectra::market
