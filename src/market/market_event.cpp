// Fixed-point price parsing/formatting and enum names.
#include "vectra/market/market_event.hpp"

#include <climits>
#include <cstdio>

namespace vectra::market {

const char* to_string(Validity v) {
  switch (v) {
    case Validity::Ok:              return "Ok";
    case Validity::Malformed:       return "Malformed";
    case Validity::WrongSymbol:     return "WrongSymbol";
    case Validity::NonPositivePrice:return "NonPositivePrice";
    case Validity::NegativeSpread:  return "NegativeSpread";
    case Validity::WideSpread:      return "WideSpread";
    case Validity::Duplicate:       return "Duplicate";
    case Validity::OutOfOrder:      return "OutOfOrder";
    case Validity::Stale:           return "Stale";
    case Validity::FutureTimestamp: return "FutureTimestamp";
  }
  return "Unknown";
}

bool parse_price(std::string_view text, Price& out) {
  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n && (text[i] == ' ' || text[i] == '\t')) {
    ++i;
  }
  bool negative = false;
  if (i < n && (text[i] == '+' || text[i] == '-')) {
    negative = (text[i] == '-');
    ++i;
  }
  if (i >= n) {
    return false;
  }

  std::int64_t whole = 0;
  int whole_digits = 0;
  while (i < n && text[i] >= '0' && text[i] <= '9') {
    const std::int64_t digit = text[i] - '0';
    if (whole > (INT64_MAX - digit) / 10) {
      return false;  // overflow
    }
    whole = whole * 10 + digit;
    ++whole_digits;
    ++i;
  }
  if (whole_digits == 0) {
    return false;
  }

  std::int64_t fraction = 0;
  int frac_digits = 0;
  if (i < n && text[i] == '.') {
    ++i;
    while (i < n && text[i] >= '0' && text[i] <= '9') {
      if (frac_digits >= kPriceDecimals) {
        return false;  // more precision than the representation supports
      }
      fraction = fraction * 10 + (text[i] - '0');
      ++frac_digits;
      ++i;
    }
    if (frac_digits == 0) {
      return false;  // "1." is malformed
    }
    while (frac_digits < kPriceDecimals) {
      fraction *= 10;
      ++frac_digits;
    }
  }

  // Reject trailing garbage.
  while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) {
    ++i;
  }
  if (i != n) {
    return false;
  }

  if (whole > (INT64_MAX - fraction) / kPriceScale) {
    return false;
  }
  const Price magnitude = whole * kPriceScale + fraction;
  out = negative ? -magnitude : magnitude;
  return true;
}

std::string format_price(Price price) {
  const bool negative = price < 0;
  std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(-price)
                                     : static_cast<std::uint64_t>(price);
  const std::uint64_t scale = static_cast<std::uint64_t>(kPriceScale);
  const std::uint64_t whole = magnitude / scale;
  std::uint64_t frac = magnitude % scale;

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%llu.%07llu", static_cast<unsigned long long>(whole),
                static_cast<unsigned long long>(frac));
  std::string result(buffer);

  // Trim trailing zeros but keep at least 5 decimals (forex convention).
  const std::size_t min_len = result.find('.') + 1 + 5;
  while (result.size() > min_len && result.back() == '0') {
    result.pop_back();
  }
  if (negative) {
    result.insert(result.begin(), '-');
  }
  return result;
}

}  // namespace vectra::market
