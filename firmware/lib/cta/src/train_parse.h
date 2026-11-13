#pragma once
// Train Tracker responses, outputType=JSON (spec 5, 6.2, 6.4).
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"

namespace cta {

// Fills out from a ttarrivals response: type = Train, ts = now, and up to kMaxArrivals arrivals, soonest first,
// with min = floor(ctaSecondsBetween(tmst, arrT) / 60) clamped to 0–32767, live = isSch == "0",
// delayed = isDly == "1", dest = destNm. name (staNm) and route (rt) come from the soonest arrival; with no
// arrivals they stay as the caller set them, as do id, dir, and alert. An eta whose arrT doesn't parse is
// skipped. eta may be an array, one object, null, or missing (count = 0). Returns false, leaving out untouched,
// on malformed JSON, errCd other than "0", or arrivals without a parseable tmst.
// routeFilter (nullptr = all) keeps only that line's trains, matched case-insensitively, and leaves route as the
// caller set it: a platform several lines share (Brown and Purple) gives the picked line's card its own trains.
bool parseTrainArrivals(const char* json, size_t len, uint32_t now, StopRecord& out, const char* routeFilter = nullptr);

// Writes up to max trains' nextStpId from a ttpositions response; route and train may each be an array or one
// object. Returns the number written, or −1 on malformed JSON or errCd other than "0".
int parseTrainPositions(const char* json, size_t len, TrainPos* out, size_t max);

}  // namespace cta
