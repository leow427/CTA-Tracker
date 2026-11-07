#pragma once
// Pet moods (spec 6.3). Mirrored by the app's MoodRules. Asleep (docked) is the pet layer's call, not decided here.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"

namespace cta {

// True when the record was never fetched (ts == 0), is more than kStaleSec old, or is more than kStaleSec ahead
// of now. Up to kStaleSec ahead (a relayed snapshot from a phone whose clock runs a little ahead) is fresh; further
// ahead can't be trusted (a handheld before its first NTP sync reads 1970 while snapshots carry real times).
bool isStale(const StopRecord& r, uint32_t now);

// In order: Sleepy if stale or night; Sad if alert or no arrivals; Worried if the next arrival is delayed;
// otherwise Happy (timetable-only arrivals included, plan 1 decision 2).
Mood stopMood(const StopRecord& r, uint32_t now, bool night);

// In order: Sleepy with no location (hasLocation is false only on a handheld with no nearby list yet; the dock
// always passes true) or at night; Hyped if any eligible, fresh record's next arrival is live and
// due within kHypedMaxMin; otherwise the majority of the stops' non-Sleepy moods, ties to Sad, then Worried, then
// Happy; Sleepy if every stop is Sleepy or n == 0. hypedEligible marks the stops that may hype (the 3 closest on
// the handheld, all 3 on the dock); nullptr means none.
Mood deviceMood(const StopRecord* recs, const bool* hypedEligible, size_t n, uint32_t now, bool night,
                bool hasLocation);

}  // namespace cta
