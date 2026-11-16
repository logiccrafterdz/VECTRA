// FX session calendar anchored to New York local time.
//
// The FX trading day rolls over at 17:00 America/New_York. In UTC that is
// 22:00 in winter (EST, UTC-5) and 21:00 in summer (EDT, UTC-4), so any
// exclusion window defined in fixed UTC hours is wrong for part of the year.
// These helpers apply the US daylight-saving rule in force since 2007
// (second Sunday of March 02:00 local -> first Sunday of November 02:00
// local) without depending on a time-zone database, so results are identical
// on every toolchain.
#pragma once

#include <cstdint>

namespace vectra::market {

// UTC offset of America/New_York at the given UTC instant, in nanoseconds
// (-5 h during EST, -4 h during EDT). Valid for years 2007 and later.
std::int64_t new_york_utc_offset_ns(std::int64_t utc_ns);

struct NewYorkClock {
  int weekday = 0;         // 0 = Monday ... 6 = Sunday (ISO order)
  int minute_of_day = 0;   // 0 .. 1439, local New York wall clock
};

NewYorkClock new_york_clock(std::int64_t utc_ns);

// Pre-registered blackout calendar (docs/experiment_preregistration.md):
//  * daily rollover window [start_min, end_min) New York local time, every day;
//  * weekend from Friday start_min until Sunday end_min New York local time.
// start_min / end_min are minutes after local midnight; start_min < end_min.
bool in_new_york_blackout(std::int64_t utc_ns, int start_min, int end_min);

}  // namespace vectra::market
