#pragma once
// The dock's bus picker lists, live from Bus Tracker v2 (spec 7.2): getroutes, getdirections, getstops.
#include <stddef.h>

#include "cta_types.h"

namespace cta {

struct BusRoute {
    char rt[8];
    char name[kNameLen + 1];   // rtnm, cut to kNameLen bytes
};
struct BusDirection {
    char name[16];             // "Southbound"
};
struct BusStop {
    char id[8];
    char name[kNameLen + 1];
    float lat, lon;
};

// Each writes up to max entries in response order and returns how many, or -1 on malformed JSON or a
// bustime-response error with no list (a bad route, key, or the daily limit). An entry missing its id is skipped.
int parseBusRoutes(const char* json, size_t len, BusRoute* out, size_t max);
// v2 sends {"dir": ...}; v3 {"id", "name"}: either is read.
int parseBusDirections(const char* json, size_t len, BusDirection* out, size_t max);
int parseBusStops(const char* json, size_t len, BusStop* out, size_t max);

// Whether a picker list fetched for (listRoute, listDir) answers the picker's current (route, dir): a reply to an
// earlier request (the person went back and chose again) must not stand in for the new one. dir "" for lists that
// don't take one.
bool pickerListFor(const char* listRoute, const char* listDir, const char* route, const char* dir);

}  // namespace cta
