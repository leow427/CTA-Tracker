#pragma once
// CTA timestamps and night hours (spec 6.6).
#include <stdint.h>

namespace cta {

// Parses a CTA timestamp into naive seconds: the Chicago wall time read as if it were UTC (days-from-civil).
// Naive seconds ignore daylight saving; use ctaSecondsBetween for elapsed time. Accepts exactly
// "YYYY-MM-DDTHH:MM:SS" (Train Tracker), "YYYYMMDD HH:MM", and "YYYYMMDD HH:MM:SS" (Bus Tracker), with real
// calendar dates. On failure returns false and leaves secs untouched.
bool parseCtaTime(const char* s, int64_t& secs);

// Real seconds from CTA timestamp `from` (a response's own time) to `to` (an arrival), both Chicago wall times
// with no offset (spec 6.6). Applies Chicago's daylight saving rules (CDT from the second Sunday of March at
// 2:00 to the first Sunday of November at 2:00), so 1:58 → 3:03 on the spring-forward night is 5 min. In the
// repeated 1:00–1:59 hour of the fall-back night, each time could be either reading, an hour apart; the
// readings whose difference is nearest 25 min win, so any true gap from −4 to +55 min comes out right, and a
// tie goes to the later reading. A wall time in the skipped spring hour (which CTA never sends) is read as
// standard time. Negative when `to` is before `from`. On failure returns false and leaves secs untouched.
bool ctaSecondsBetween(const char* from, const char* to, int64_t& secs);

// Parses an HTTP Date header (RFC 9110's IMF-fixdate, "Sun, 06 Nov 1994 08:49:37 GMT") into Unix seconds: the
// handheld's clock when NTP is blocked. False, leaving unix untouched, for anything else.
bool parseHttpDate(const char* s, int64_t& unix);

// True when minuteOfDay (0–1439) is in [startMin, endMin), wrapping past midnight when start > end.
// start == end means never.
bool inNightHours(int minuteOfDay, int startMin, int endMin);

}  // namespace cta
