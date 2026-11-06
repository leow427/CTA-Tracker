#pragma once
#include <stddef.h>

#include "cta_types.h"

namespace cta {
// getpredictions: one record per distinct stpid, in order of first appearance. Sets type, id, name (stpnm),
// route and dir (of the soonest kept prediction) and ts = now. Up to 4 arrivals sorted by minutes; "DUE" is 0,
// otherwise floor((prdtm - tmstmp) / 60) clamped at 0; live = true; delayed = dly or "DLY".
// routeFilter (nullptr = all) drops other routes. Returns the count; 0 when the only errors name a stop or
// route ("No arrival times"); -1 if malformed or for an error that names neither (bad key, daily limit).
int parseBusPredictions(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord* out,
                        size_t max);
// getvehicles: pid and pdist as numbers or numeric strings. Returns the count, or -1 on error.
int parseBusVehicles(const char* json, size_t len, BusVehicle* out, size_t max);

// The dock's bus picker (getroutes / getdirections / getstops).
struct BusRoute { char rt[kIdLen + 1]; char name[kNameLen + 1]; };
struct BusStop { char id[kIdLen + 1]; char name[kNameLen + 1]; };
int parseBusRoutes(const char* json, size_t len, BusRoute* out, size_t max);
int parseBusDirections(const char* json, size_t len, char (*out)[kNameLen + 1], size_t max);
int parseBusStops(const char* json, size_t len, BusStop* out, size_t max);
}  // namespace cta
