#pragma once
// The handheld's `nearby` message (spec 8.3, 9): where it is, its 10 nearest train stations and bus stops, nearest
// first, and the 3 closest stops of either kind. The app writes it over BLE; for the demo the laptop sends the same
// JSON over USB serial.
//   {"lat":41.94,"lon":-87.65,"trains":["41320",…],"buses":["14915",…],"close":["14915","1921","41320"]}
#include <stddef.h>

#include "cta_config.h"

namespace cta {

struct Nearby {
    static constexpr int kIdLen = 8;   // a station ID is 5 digits, a bus stop ID up to 5 or 6
    double lat = 0, lon = 0;
    char trains[kNearbyPerTab][kIdLen] = {};
    char buses[kNearbyPerTab][kIdLen] = {};
    char close[kHypedClosestStops][kIdLen] = {};
    int nTrains = 0, nBuses = 0, nClose = 0;
};

// Fills out from one message. False, leaving out untouched, unless: lat and lon are numbers within range; trains
// are station IDs (5 digits, 4xxxx) and buses all-digit stop IDs, each list at most kNearbyPerTab long with no
// repeats; and every close ID (at most kHypedClosestStops) is in one of the two lists. Either list may be empty.
bool parseNearby(const char* json, size_t len, Nearby& out);

}  // namespace cta
