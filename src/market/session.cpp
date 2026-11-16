// FX session calendar anchored to New York local time.
#include "vectra/market/session.hpp"

namespace vectra::market {

namespace {

constexpr std::int64_t kNsPerSecond = 1000000000LL;
constexpr std::int64_t kNsPerHour = 3600LL * kNsPerSecond;
constexpr std::int64_t kNsPerDay = 24LL * kNsPerHour;

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
  std::int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) {
    --q;
  }
  return q;
}

// Days since 1970-01-01 for a proleptic Gregorian civil date (H. Hinnant).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::int64_t year_from_days(std::int64_t z) {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return y + (m <= 2 ? 1 : 0);
}

// ISO weekday (0 = Monday) of a day count since 1970-01-01 (a Thursday).
int weekday_from_days(std::int64_t days) {
  return static_cast<int>(((days % 7) + 7 + 3) % 7);
}

// Day count of the n-th Sunday (n >= 1) of the given month.
std::int64_t nth_sunday(std::int64_t year, unsigned month, int n) {
  const std::int64_t first = days_from_civil(year, month, 1);
  const int wd = weekday_from_days(first);
  const int to_sunday = (6 - wd + 7) % 7;
  return first + to_sunday + 7LL * (n - 1);
}

}  // namespace

std::int64_t new_york_utc_offset_ns(std::int64_t utc_ns) {
  const std::int64_t year = year_from_days(floor_div(utc_ns, kNsPerDay));
  // DST starts 02:00 EST (07:00 UTC) on the second Sunday of March and ends
  // 02:00 EDT (06:00 UTC) on the first Sunday of November.
  const std::int64_t dst_start = nth_sunday(year, 3, 2) * kNsPerDay + 7 * kNsPerHour;
  const std::int64_t dst_end = nth_sunday(year, 11, 1) * kNsPerDay + 6 * kNsPerHour;
  const bool dst = utc_ns >= dst_start && utc_ns < dst_end;
  return (dst ? -4 : -5) * kNsPerHour;
}

NewYorkClock new_york_clock(std::int64_t utc_ns) {
  const std::int64_t local_ns = utc_ns + new_york_utc_offset_ns(utc_ns);
  const std::int64_t days = floor_div(local_ns, kNsPerDay);
  const std::int64_t ns_of_day = local_ns - days * kNsPerDay;
  NewYorkClock clock;
  clock.weekday = weekday_from_days(days);
  clock.minute_of_day = static_cast<int>(ns_of_day / (60 * kNsPerSecond));
  return clock;
}

bool in_new_york_blackout(std::int64_t utc_ns, int start_min, int end_min) {
  const NewYorkClock c = new_york_clock(utc_ns);
  const int m = c.minute_of_day;
  if (c.weekday == 5) {  // Saturday
    return true;
  }
  if (c.weekday == 4 && m >= start_min) {  // Friday from the rollover start
    return true;
  }
  if (c.weekday == 6 && m < end_min) {  // Sunday until the reopening settles
    return true;
  }
  return m >= start_min && m < end_min;
}

}  // namespace vectra::market
