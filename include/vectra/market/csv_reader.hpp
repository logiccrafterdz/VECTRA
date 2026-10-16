// CSV tick reader.
//
// Expected columns (header optional):
//   symbol,bid,ask,source_ts_ns[,sequence]
//
// Rows that fail to parse are reported as Malformed events with a note so the
// replay can count them instead of silently skipping them.
#pragma once

#include <cstdint>
#include <functional>
#include <istream>
#include <string>

#include "vectra/market/market_event.hpp"

namespace vectra::market {

struct ParsedLine {
  MarketEvent event;        // valid only when ok == true
  bool ok = false;          // false => row could not be parsed at all
  std::string error;        // parse error detail
  std::size_t line_number = 0;
};

// Parses one CSV row. Blank lines and '#' comments return ok=false with an
// empty error and is_skippable semantics handled by the caller via
// `is_skippable` below.
ParsedLine parse_tick_line(std::string_view line, std::size_t line_number,
                           std::int64_t local_receive_ns);

// True for blank lines and comment lines (should be ignored, not counted).
bool is_skippable_line(std::string_view line);

// Reads `input` line by line and invokes `on_line` for every non-skippable
// row. Returns the number of rows delivered.
std::size_t read_ticks(std::istream& input, std::int64_t local_receive_ns,
                       const std::function<void(const ParsedLine&)>& on_line);

}  // namespace vectra::market
