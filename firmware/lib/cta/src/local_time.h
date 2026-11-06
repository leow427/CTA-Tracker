#pragma once
// CTA timestamps and night hours (spec 6.6).
#include <stdint.h>

namespace cta {

// Parses a CTA timestamp into naive seconds: the local wall time treated as UTC (days-from-civil), so only
// differences between two parsed times mean anything. Accepts exactly "YYYY-MM-DDTHH:MM:SS" (Train Tracker),
// "YYYYMMDD HH:MM", and "YYYYMMDD HH:MM:SS" (Bus Tracker), with real calendar dates. On failure returns false
// and leaves secs untouched.
bool parseCtaTime(const char* s, int64_t& secs);

// True when minuteOfDay (0–1439) is in [startMin, endMin), wrapping past midnight when start > end.
// start == end means never.
bool inNightHours(int minuteOfDay, int startMin, int endMin);

}  // namespace cta
