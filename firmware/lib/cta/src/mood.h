#pragma once
#include <stddef.h>

#include "cta_types.h"

namespace cta {
// Spec 6.3, checked in order: Sleepy (never fetched, older than 5 min, or night), Sad (alert or no
// predictions), Worried (next arrival delayed), Happy (including timetable-only arrivals: plan 1 decision 2).
Mood stopMood(const StopRecord& r, uint32_t now, bool night);
// Sleepy without location or at night; Hyped if an eligible, fresh stop's next arrival is live and due in
// <= 3 min; otherwise the majority of the non-Sleepy stop moods, ties to Sad, then Worried, then Happy;
// Sleepy when every stop is Sleepy. Asleep (docked) belongs to the pet layer.
Mood deviceMood(const StopRecord* recs, const bool* hypedEligible, size_t n, uint32_t now, bool night,
                bool hasLocation);
}  // namespace cta
