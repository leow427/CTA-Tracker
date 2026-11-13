#pragma once
// The dock's 3 picked stops (spec 6.1, 7.2), as kept in flash: a small JSON list, one entry per section.
#include <stddef.h>

#include "cta_types.h"

namespace cta {

constexpr int kDockStops = 3;

struct PickedStop {
    Kind type;
    char route[8];               // GTFS route_id or bus route
    char dir[40];                // a train direction label (Green's longest is 28 bytes) or a bus rtdir
    char id[8];                  // a platform ID (3xxxx) or bus stop ID; "" for an empty section
    char name[kNameLen + 1];     // the stop's name
    char routeName[kNameLen + 1];   // a bus route's name ("Clark"); "" for trains
};

// The same stop for the same line and direction (both empty counts as the same).
bool samePick(const PickedStop& a, const PickedStop& b);

// Until the picker is used: Red at Belmont toward 95th/Dan Ryan, Brown at Diversey toward the Loop, and the 22 at
// Clark & Belmont southbound (the IDs from data/stops.json).
void defaultPicks(PickedStop out[kDockStops]);

// [{"type":"train"|"bus","route","dir","id","name","rtnm"} or {} for an empty section, ×3]. Returns the length
// (and a NUL), or 0 if it doesn't fit in cap.
size_t encodePicks(const PickedStop in[kDockStops], char* buf, size_t cap);
// The reverse: exactly 3 entries, each an object (or null) with an id and a type of train or bus, or empty. False,
// leaving out untouched, otherwise.
bool decodePicks(const char* json, size_t len, PickedStop out[kDockStops]);

}  // namespace cta
