#pragma once
// A station's Train Tracker arrivals split into card records, one per line and direction (spec 6.2, handheld plan).
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"

namespace cta {

// The direction label for a platform on a line, from kTrainLines ("Howard", "Loop"): the sequence where the
// platform is a pickable stop wins over one where it's only lead-in. nullptr if no sequence has it.
const char* trainDirLabel(const char* route, const char* stpId);

// One record per (platform stpId, rt) in a ttarrivals?mapid= response, soonest card first: id = stationId,
// type = Train, route = rt, dir = trainDirLabel(rt, stpId) (or the soonest arrival's destNm when no sequence has
// the platform), name = staNm, ts = now, alert = false, and up to kMaxArrivals arrivals, soonest first, with the
// same per-arrival rules as parseTrainArrivals. Writes at most max cards (the soonest). Returns the number
// written, or -1 on malformed JSON, errCd other than "0", or arrivals without a parseable tmst.
int parseTrainCards(const char* json, size_t len, uint32_t now, const char* stationId, StopRecord* out, size_t max);

}  // namespace cta
