#pragma once
// Bus Tracker v2 responses, format=json (spec 5, 6.2, 6.4).
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"

namespace cta {

// One record per distinct (stpid, rt, rtdir), in order of first appearance, up to max: each route and direction at
// a stop is its own card (spec 6.2). Each has type = Bus, id, route, dir, name (stpnm),
// ts = now, alert = false (the caller merges alerts), and up to kMaxArrivals arrivals, soonest first by seconds
// (DUE counts as 0 s, a passed prediction as negative; ties keep response order): min = 0 for prdctdn "DUE", otherwise
// floor(ctaSecondsBetween(tmstmp, prdtm) / 60) clamped to 0–32767; live = true; delayed = dly || prdctdn "DLY";
// dest = des (where it ends). routeFilter (nullptr = all) drops other routes; a prediction with no rt is skipped.
// A prediction whose time doesn't parse is skipped. prd may be a list or one object.
// Returns the count. With no prd: 0 when every error names a stop, route, or pattern ("No arrival times"), and −1
// for any other error (a bad key, the daily limit), so a failed fetch isn't read as "no buses". −1 if malformed.
int parseBusPredictions(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord* out,
                        size_t max);

// Writes up to max vehicles' pid and pdist, each a number or a numeric string; a vehicle without both is
// skipped. Returns the number written, with the same error rules as parseBusPredictions.
int parseBusVehicles(const char* json, size_t len, BusVehicle* out, size_t max);

}  // namespace cta
