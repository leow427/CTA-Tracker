#include "store.h"

#include <Preferences.h>

namespace store {

namespace {
constexpr const char* kNs = "dock";
}

void loadPicks(cta::PickedStop out[cta::kDockStops]) {
    cta::defaultPicks(out);
    Preferences p;
    if (!p.begin(kNs, true)) return;
    const String s = p.getString("picks", "");
    p.end();
    if (s.length() && !cta::decodePicks(s.c_str(), s.length(), out)) cta::defaultPicks(out);
}

bool savePicks(const cta::PickedStop picks[cta::kDockStops], bool* wrote) {
    if (wrote) *wrote = false;
    char buf[640];
    const size_t n = cta::encodePicks(picks, buf, sizeof(buf));
    if (!n) return false;
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    // Unchanged picks aren't written again: a flash write upsets the panel (display.h).
    if (p.getString("picks", "") == buf) {
        p.end();
        return true;
    }
    const bool ok = p.putString("picks", buf) == n;
    p.end();
    if (wrote) *wrote = true;
    return ok;
}

bool loadGeo(float& lat, float& lon, uint32_t& when) {
    Preferences p;
    if (!p.begin(kNs, true)) return false;
    const bool has = p.isKey("geoLat");
    if (has) {
        lat = p.getFloat("geoLat", 0);
        lon = p.getFloat("geoLon", 0);
        when = p.getUInt("geoT", 0);
    }
    p.end();
    return has;
}

bool saveGeo(float lat, float lon, uint32_t when) {
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    const bool ok = p.putFloat("geoLat", lat) && p.putFloat("geoLon", lon) && p.putUInt("geoT", when);
    p.end();
    return ok;
}

}  // namespace store
