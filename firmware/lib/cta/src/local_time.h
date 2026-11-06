#pragma once
#include <stdint.h>

namespace cta {

// A CTA wall-clock time as naive seconds (read as if UTC). Only differences mean anything.
// Accepts "2026-11-05T08:00:00" (Train Tracker), "20261105 08:00" and "20261105 08:04:30" (Bus Tracker).
bool parseCtaTime(const char* s, int64_t& secs);

// Night hours are [start, end) in minutes after midnight; they may wrap midnight; start == end means never.
bool inNightHours(int minuteOfDay, int startMin, int endMin);

}  // namespace cta
