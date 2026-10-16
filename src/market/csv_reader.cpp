// CSV tick parsing.
#include "vectra/market/csv_reader.hpp"

#include <sstream>
#include <vector>

namespace vectra::market {

namespace {

std::string trim(std::string_view s) {
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) {
    ++begin;
  }
  while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
    --end;
  }
  return std::string(s.substr(begin, end - begin));
}

std::vector<std::string> split_csv(std::string_view line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= line.size(); ++i) {
    if (i == line.size() || line[i] == ',') {
      fields.push_back(trim(line.substr(start, i - start)));
      start = i + 1;
    }
  }
  return fields;
}

bool parse_i64(const std::string& text, std::int64_t& out) {
  if (text.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const long long value = std::stoll(text, &pos, 10);
    if (pos != text.size()) {
      return false;
    }
    out = static_cast<std::int64_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_u64(const std::string& text, std::uint64_t& out) {
  if (text.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const unsigned long long value = std::stoull(text, &pos, 10);
    if (pos != text.size()) {
      return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool looks_like_header(const std::vector<std::string>& fields) {
  if (fields.empty()) {
    return false;
  }
  std::string first = fields[0];
  for (char& c : first) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return first == "symbol" || first == "instrument";
}

}  // namespace

bool is_skippable_line(std::string_view line) {
  const std::string t = trim(line);
  return t.empty() || t[0] == '#';
}

ParsedLine parse_tick_line(std::string_view line, std::size_t line_number,
                           std::int64_t local_receive_ns) {
  ParsedLine parsed;
  parsed.line_number = line_number;

  const std::string t = trim(line);
  if (t.empty() || t[0] == '#') {
    parsed.ok = false;
    parsed.error = "skippable";
    return parsed;
  }

  const std::vector<std::string> fields = split_csv(t);
  if (looks_like_header(fields)) {
    parsed.ok = false;
    parsed.error = "header";
    return parsed;
  }
  if (fields.size() < 4 || fields.size() > 5) {
    parsed.ok = false;
    parsed.error = "expected 4 or 5 columns (symbol,bid,ask,source_ts_ns[,sequence])";
    return parsed;
  }

  MarketEvent event;
  event.symbol = fields[0];
  if (event.symbol.empty()) {
    parsed.ok = false;
    parsed.error = "empty symbol";
    return parsed;
  }
  if (!parse_price(fields[1], event.bid)) {
    parsed.ok = false;
    parsed.error = "invalid bid '" + fields[1] + "'";
    return parsed;
  }
  if (!parse_price(fields[2], event.ask)) {
    parsed.ok = false;
    parsed.error = "invalid ask '" + fields[2] + "'";
    return parsed;
  }
  if (!parse_i64(fields[3], event.source_time_ns)) {
    parsed.ok = false;
    parsed.error = "invalid source_ts_ns '" + fields[3] + "'";
    return parsed;
  }
  event.has_source_time = true;
  if (fields.size() == 5) {
    std::uint64_t seq = 0;
    if (!parse_u64(fields[4], seq)) {
      parsed.ok = false;
      parsed.error = "invalid sequence '" + fields[4] + "'";
      return parsed;
    }
    event.sequence = seq;
  }
  event.local_receive_ns = local_receive_ns;

  parsed.event = std::move(event);
  parsed.ok = true;
  return parsed;
}

std::size_t read_ticks(std::istream& input, std::int64_t local_receive_ns,
                       const std::function<void(const ParsedLine&)>& on_line) {
  std::string line;
  std::size_t line_number = 0;
  std::size_t delivered = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (is_skippable_line(line)) {
      continue;
    }
    ParsedLine parsed = parse_tick_line(line, line_number, local_receive_ns);
    if (!parsed.ok && (parsed.error == "skippable" || parsed.error == "header")) {
      continue;
    }
    on_line(parsed);
    ++delivered;
  }
  return delivered;
}

}  // namespace vectra::market
