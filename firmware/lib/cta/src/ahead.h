#pragma once
// The dock's touch extras (ecosystem spec 5.10): one vehicle's stops ahead (Follow This Train, bus predictions by
// vehicle), and the stop the other way for a card's 30 s peek.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"
#include "picks.h"

namespace cta {

constexpr size_t kAheadStops = 5;

struct AheadStop {
    char name[kNameLen + 1];
    int16_t min;   // minutes from the reply's own timestamp, floored, 0–32767
};

// A ttfollow.aspx (outputType=JSON) reply: the train's next stops, soonest first (staNm, arrT against tmst), up to
// max. Returns the count; 0 when the train is gone (errCd 501, no trains with that run number, or no eta); −1 if
// malformed or any other error.
int parseTrainAhead(const char* json, size_t len, AheadStop* out, size_t max);
// A getpredictions?vid= reply: the bus's next stops, soonest first (stpnm; prdctdn "DUE" is 0), up to max. Returns
// the count; 0 when every error names the vehicle (it's off the road); −1 if malformed or any other error.
int parseBusAhead(const char* json, size_t len, AheadStop* out, size_t max);

// The station's platform the other way for a picked train platform: the closest platform (by kPlatforms, the same
// station sits at one point) on a sequence of the same route with another direction label, within 300 m. Fills
// out's type, route, dir (that label), id and name. False if there's none.
bool oppositeTrainPlatform(const char* route, const char* stopId, const char* dirLabel, PickedStop& out);

// From a getpatterns reply: the closest stop (typ "S") on a pattern of the same route going another rtdir than dir,
// to stopId's own position, within 500 m. Fills out's type, route, dir (that rtdir), id and name (stpnm); routeName
// is left as it was. False if stopId isn't on any pattern, or there's no such stop.
bool oppositeBusStop(const char* patternsJson, size_t len, const char* route, const char* stopId, const char* dir,
                     PickedStop& out);

}  // namespace cta
