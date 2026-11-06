#pragma once
#include <stddef.h>

#include "cta_types.h"

namespace cta {
// ttarrivals. Sets type, name (staNm), route (rt of the soonest kept arrival) and ts = now, and up to 4
// arrivals sorted by minutes: min = floor((arrT - tmst) / 60) clamped at 0, live = isSch "0", delayed = isDly "1".
// routeFilter (nullptr = all) keeps one line, for a platform that several lines share.
// Leaves id, dir and alert as the caller set them. False on an API error (errCd != "0") or malformed JSON;
// true with count 0 when there are no etas.
bool parseTrainArrivals(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord& out);
// ttpositions: each train's next stop and line. `route` and `train` may each be one object. Returns the count written, or -1 on error.
int parseTrainPositions(const char* json, size_t len, TrainPos* out, size_t max);
}  // namespace cta
