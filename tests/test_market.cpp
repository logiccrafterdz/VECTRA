// Market data layer tests: fixed-point prices, validation rules, CSV parsing.
#include "test_util.hpp"
#include "vectra_test.hpp"

#include <sstream>

#include "vectra/market/csv_reader.hpp"
#include "vectra/market/validator.hpp"

using vectra::market::EventValidator;
using vectra::market::MarketEvent;
using vectra::market::Price;
using vectra::market::Validity;
using vectra::market::ValidatorConfig;
using vectra::test::make_event;
using vectra::test::make_quote;

// ---------------------------------------------------------------------------
// Fixed-point price parsing / formatting
// ---------------------------------------------------------------------------

VT_TEST(Price, ParsesStandardForexQuotes) {
  vectra::market::Price price = 0;
  VT_ASSERT_TRUE(vectra::market::parse_price("1.10010", price));
  VT_ASSERT_EQ(price, Price{11'001'000});

  VT_ASSERT_TRUE(vectra::market::parse_price("1.1", price));
  VT_ASSERT_EQ(price, Price{11'000'000});

  VT_ASSERT_TRUE(vectra::market::parse_price("100", price));
  VT_ASSERT_EQ(price, Price{1'000'000'000});

  VT_ASSERT_TRUE(vectra::market::parse_price("-1.5", price));
  VT_ASSERT_EQ(price, Price{-15'000'000});
}

VT_TEST(Price, RejectsMalformedInput) {
  vectra::market::Price price = 0;
  VT_ASSERT_FALSE(vectra::market::parse_price("abc", price));
  VT_ASSERT_FALSE(vectra::market::parse_price("", price));
  VT_ASSERT_FALSE(vectra::market::parse_price(".5", price));
  VT_ASSERT_FALSE(vectra::market::parse_price("1.", price));
  VT_ASSERT_FALSE(vectra::market::parse_price("1.0x", price));
  // More precision than the representation supports must be rejected, never
  // silently rounded.
  VT_ASSERT_FALSE(vectra::market::parse_price("1.12345678", price));
  VT_ASSERT_TRUE(vectra::market::parse_price("1.1234567", price));
}

VT_TEST(Price, FormattingRoundTrips) {
  vectra::market::Price price = 0;
  VT_ASSERT_TRUE(vectra::market::parse_price("1.10010", price));
  VT_ASSERT_STREQ(vectra::market::format_price(price), std::string("1.10010"));
  VT_ASSERT_STREQ(vectra::market::format_price(vectra::market::double_to_price(1.1)),
                  std::string("1.10000"));
  VT_ASSERT_STREQ(vectra::market::format_price(100 * vectra::market::kPriceScale),
                  std::string("100.00000"));
}

VT_TEST(Price, IntegerMidpointIsExact) {
  const MarketEvent event = make_event(1.10000, 1.10010, 1);
  // (11000000 + 11001000) / 2 = 11000500 exactly: no floating-point equality.
  VT_ASSERT_EQ(event.mid_fixed(), Price{11'000'500});
  VT_ASSERT_EQ(event.spread(), Price{1'000});
}

// ---------------------------------------------------------------------------
// Bid/ask validation
// ---------------------------------------------------------------------------

VT_TEST(Validator, AcceptsHealthyQuote) {
  EventValidator validator{ValidatorConfig{}};
  const auto result = validator.validate(make_quote(1.10, 0.00010, 1'000'000'000, 1));
  VT_ASSERT_TRUE(result.ok());
  VT_ASSERT_EQ(validator.accepted(), std::size_t{1});
  VT_ASSERT_EQ(validator.rejected(), std::size_t{0});
}

VT_TEST(Validator, RejectsCrossedAndNonPositiveQuotes) {
  ValidatorConfig config;
  EventValidator validator{config};

  VT_ASSERT_EQ(validator.validate(make_event(1.10010, 1.10000, 1, 1)).validity,
               Validity::NegativeSpread);
  VT_ASSERT_EQ(validator.validate(make_event(0.0, 1.10010, 2, 2)).validity,
               Validity::NonPositivePrice);
  VT_ASSERT_EQ(validator.validate(make_event(1.10000, 0.0, 3, 3)).validity,
               Validity::NonPositivePrice);
}

VT_TEST(Validator, RejectsWideSpread) {
  ValidatorConfig config;
  config.max_spread = vectra::market::double_to_price(0.00050);
  EventValidator validator{config};
  const auto result = validator.validate(make_quote(1.10, 0.00080, 1, 1));
  VT_ASSERT_EQ(result.validity, Validity::WideSpread);
}

VT_TEST(Validator, RejectsWrongSymbol) {
  ValidatorConfig config;
  config.expected_symbol = "EURUSD";
  EventValidator validator{config};
  const auto result =
      validator.validate(make_event(1.09995, 1.10005, 1, 1, "GBPUSD"));
  VT_ASSERT_EQ(result.validity, Validity::WrongSymbol);
}

VT_TEST(Validator, RejectsMissingTimestampWhenRequired) {
  ValidatorConfig config;
  config.require_source_time = true;
  EventValidator validator{config};
  MarketEvent event = make_quote(1.10, 0.00010, 1, 1);
  event.has_source_time = false;
  VT_ASSERT_EQ(validator.validate(event).validity, Validity::Malformed);
}

// ---------------------------------------------------------------------------
// Duplicates, ordering, staleness
// ---------------------------------------------------------------------------

VT_TEST(Validator, RejectsDuplicateSequence) {
  EventValidator validator{ValidatorConfig{}};
  validator.validate(make_quote(1.10, 0.00010, 1'000'000'000, 7));
  const auto result = validator.validate(make_quote(1.1001, 0.00010, 2'000'000'000, 7));
  VT_ASSERT_EQ(result.validity, Validity::Duplicate);
}

VT_TEST(Validator, RejectsDuplicateTimestampWithoutSequence) {
  EventValidator validator{ValidatorConfig{}};
  validator.validate(make_quote(1.10, 0.00010, 1'000'000'000));
  const auto result = validator.validate(make_quote(1.1001, 0.00010, 1'000'000'000));
  VT_ASSERT_EQ(result.validity, Validity::Duplicate);
}

VT_TEST(Validator, RejectsOutOfOrderSequence) {
  EventValidator validator{ValidatorConfig{}};
  validator.validate(make_quote(1.10, 0.00010, 1'000'000'000, 10));
  const auto result = validator.validate(make_quote(1.1001, 0.00010, 2'000'000'000, 5));
  VT_ASSERT_EQ(result.validity, Validity::OutOfOrder);
}

VT_TEST(Validator, RejectsBackwardTimestamps) {
  ValidatorConfig config;
  config.reorder_tolerance_ns = 0;
  EventValidator validator{config};
  validator.validate(make_quote(1.10, 0.00010, 10'000'000'000, 1));
  const auto result = validator.validate(make_quote(1.1001, 0.00010, 9'000'000'000, 2));
  VT_ASSERT_EQ(result.validity, Validity::OutOfOrder);
}

VT_TEST(Validator, RejectsStaleTimestampBeyondThreshold) {
  ValidatorConfig config;
  config.stale_threshold_ns = 5'000'000'000;  // 5 s
  config.reorder_tolerance_ns = 0;
  EventValidator validator{config};
  validator.validate(make_quote(1.10, 0.00010, 100'000'000'000, 1));
  // 30 s behind the stream clock with an increasing sequence: staleness wins.
  const auto result = validator.validate(make_quote(1.1001, 0.00010, 70'000'000'000, 2));
  VT_ASSERT_EQ(result.validity, Validity::Stale);
}

VT_TEST(Validator, FlagsForwardGapWithoutRejecting) {
  ValidatorConfig config;
  config.stale_gap_threshold_ns = 5'000'000'000;
  EventValidator validator{config};
  validator.validate(make_quote(1.10, 0.00010, 1'000'000'000, 1));
  const auto result = validator.validate(make_quote(1.1001, 0.00010, 20'000'000'000, 2));
  VT_ASSERT_TRUE(result.ok());
  VT_ASSERT_TRUE(result.stale_gap);
}

VT_TEST(Validator, RejectsFutureTimestampAgainstReferenceClock) {
  ValidatorConfig config;
  config.future_skew_ns = 1'000'000'000;
  EventValidator validator{config};
  const std::int64_t now = 1'000'000'000;
  const auto result = validator.validate(make_quote(1.10, 0.00010, now + 5'000'000'000, 1), now);
  VT_ASSERT_EQ(result.validity, Validity::FutureTimestamp);
}

VT_TEST(Validator, RejectedEventsDoNotAdvanceStreamState) {
  EventValidator validator{ValidatorConfig{}};
  validator.validate(make_quote(1.10, 0.00010, 10'000'000'000, 5));
  // Rejected: crossed market, so it must not update the last sequence.
  VT_ASSERT_EQ(validator.validate(make_event(1.1010, 1.1000, 20'000'000'000, 6)).validity,
               Validity::NegativeSpread);
  // Sequence 6 now arrives cleanly: still not a duplicate of 5.
  const auto result = validator.validate(make_quote(1.10, 0.00010, 20'000'000'000, 6));
  VT_ASSERT_TRUE(result.ok());
}

// ---------------------------------------------------------------------------
// CSV parsing
// ---------------------------------------------------------------------------

VT_TEST(Csv, ParsesValidRowWithSequence) {
  const auto parsed = vectra::market::parse_tick_line(
      "EURUSD,1.10000,1.10010,1700000000000000000,42", 1, 7);
  VT_ASSERT_TRUE(parsed.ok);
  VT_ASSERT_STREQ(parsed.event.symbol, std::string("EURUSD"));
  VT_ASSERT_EQ(parsed.event.bid, Price{11'000'000});
  VT_ASSERT_EQ(parsed.event.ask, Price{11'001'000});
  VT_ASSERT_TRUE(parsed.event.sequence.has_value());
  VT_ASSERT_EQ(*parsed.event.sequence, std::uint64_t{42});
}

VT_TEST(Csv, ParsesRowWithoutSequence) {
  const auto parsed =
      vectra::market::parse_tick_line("EURUSD,1.10000,1.10010,1700000000000000000", 1, 0);
  VT_ASSERT_TRUE(parsed.ok);
  VT_ASSERT_FALSE(parsed.event.sequence.has_value());
}

VT_TEST(Csv, ReportsMalformedRowsInsteadOfSkipping) {
  const auto parsed = vectra::market::parse_tick_line("EURUSD,abc,1.10010,123,1", 4, 0);
  VT_ASSERT_FALSE(parsed.ok);
  VT_ASSERT_TRUE(parsed.error.find("bid") != std::string::npos);
  VT_ASSERT_EQ(parsed.line_number, std::size_t{4});
}

VT_TEST(Csv, SkipsHeaderAndCommentLines) {
  VT_ASSERT_TRUE(vectra::market::is_skippable_line("# comment"));
  VT_ASSERT_TRUE(vectra::market::is_skippable_line("   "));
  VT_ASSERT_FALSE(vectra::market::is_skippable_line("EURUSD,1,2,3"));

  std::istringstream input(
      "# comment\n"
      "symbol,bid,ask,source_ts_ns,sequence\n"
      "EURUSD,1.10000,1.10010,1000000000,1\n"
      "\n"
      "EURUSD,1.10010,1.10020,2000000000,2\n");
  std::size_t rows = 0;
  std::size_t errors = 0;
  const std::size_t delivered = vectra::market::read_ticks(input, 0, [&](const auto& line) {
    ++rows;
    if (!line.ok) {
      ++errors;
    }
  });
  VT_ASSERT_EQ(delivered, std::size_t{2});
  VT_ASSERT_EQ(rows, std::size_t{2});
  VT_ASSERT_EQ(errors, std::size_t{0});
}
