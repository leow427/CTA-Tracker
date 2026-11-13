#pragma once
// The dock's flash (NVS): its picked stops (spec 7.2) and its cached weather location (spec 7.5).
#include <stdint.h>

#include "picks.h"

namespace store {

// The saved picks, or the defaults (cta::defaultPicks) when none are saved or they don't parse.
void loadPicks(cta::PickedStop out[cta::kDockStops]);
// Writes the picks unless the saved ones are the same (wrote says whether it wrote flash).
bool savePicks(const cta::PickedStop picks[cta::kDockStops], bool* wrote = nullptr);
// The last ipapi.co location and when it was looked up (Unix time); false if none.
bool loadGeo(float& lat, float& lon, uint32_t& when);
bool saveGeo(float lat, float lon, uint32_t when);

}  // namespace store
