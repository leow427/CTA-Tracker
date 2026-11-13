#pragma once
// When a poll is due (spec 6.6): never run is due; at night everything polls every kSlowPollSec, never faster
// than normal.
#include <math.h>
#include <stdint.h>

#include "cta_config.h"

namespace cta {

inline bool pollDue(uint32_t lastRun, uint32_t intervalSec, uint32_t now, bool slow) {
    if (lastRun == 0) return true;
    const uint32_t every = slow && intervalSec < kSlowPollSec ? kSlowPollSec : intervalSec;
    return now - lastRun >= every;
}

// A failed location lookup is tried again after this.
constexpr uint32_t kGeoRetrySec = 300;

// The dock's location lookup (spec 7.5): due at boot (bootDone false) whatever is saved, then once lastOk (Unix
// time of the last success, saved or this run; 0 for none) is kGeoLookupSec old, but never within kGeoRetrySec of
// the last try (lastTry, 0 for none).
inline bool geoLookupDue(bool bootDone, uint32_t lastOk, uint32_t lastTry, uint32_t now) {
    if (!bootDone) return true;
    if (lastTry && now - lastTry < kGeoRetrySec) return false;
    return lastOk == 0 || now - lastOk >= kGeoLookupSec;
}

// Whether a new location fix is worth a flash write: nothing saved yet, or more than 1 km from the saved one (the
// dock's panel glitches through a flash write, so an unchanged location isn't written again).
inline bool geoWorthSaving(bool hadSaved, float savedLat, float savedLon, float lat, float lon) {
    if (!hadSaved) return true;
    const float kmY = (lat - savedLat) * 111.32f;
    const float kmX = (lon - savedLon) * 111.32f * cosf(savedLat * 3.14159265f / 180);
    return kmX * kmX + kmY * kmY > 1.0f;
}

// How long to wait before joining Wi-Fi again after `failures` failed tries in a row: the first join after boot
// often fails at once and the next works, so 2 s; then 15 s, doubling to 5 min. 0 failures: now.
inline uint32_t wifiRetryDelayMs(uint32_t failures) {
    if (failures == 0) return 0;
    if (failures == 1) return 2000;
    uint32_t ms = 15000;
    for (uint32_t k = 2; k < failures && ms < 300000; ++k) ms *= 2;
    return ms < 300000 ? ms : 300000;
}

}  // namespace cta
