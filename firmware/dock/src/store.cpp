#include "store.h"

#include <Preferences.h>
#include <string.h>

namespace store {

namespace {
constexpr const char* kNs = "dock";

// A fixed-size value under key; false if it's missing or a different size.
bool loadBytes(const char* key, void* out, size_t n) {
    Preferences p;
    if (!p.begin(kNs, true)) return false;
    const bool ok = p.getBytesLength(key) == n && p.getBytes(key, out, n) == n;
    p.end();
    return ok;
}

// Writes the value unless the saved one is the same.
bool saveBytes(const char* key, const void* v, size_t n) {
    uint8_t old[128];
    if (n <= sizeof(old) && loadBytes(key, old, n) && memcmp(old, v, n) == 0) return true;
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    const bool ok = p.putBytes(key, v, n) == n;
    p.end();
    return ok;
}
}  // namespace

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

bool loadWifi(cta::SavedNet& out) {
    cta::SavedNet n;
    if (!loadBytes("wifi", &n, sizeof(n)) || !n.ssid[0] || n.ssid[cta::kSsidLen] || n.pass[cta::kPassLen]) return false;
    out = n;
    return true;
}
bool saveWifi(const cta::SavedNet& n) { return saveBytes("wifi", &n, sizeof(n)); }

bool loadToken(char out[cta::kTokenLen + 1]) {
    char t[cta::kTokenLen + 1];
    if (!loadBytes("token", t, sizeof(t)) || t[cta::kTokenLen]) return false;
    memcpy(out, t, sizeof(t));
    return true;
}
bool saveToken(const char* t) {
    char v[cta::kTokenLen + 1] = {};
    strncpy(v, t, cta::kTokenLen);
    return saveBytes("token", v, sizeof(v));
}

bool loadPet(cta::PetConfig& out) {
    cta::PetConfig p;
    if (!loadBytes("pet", &p, sizeof(p)) || p.name[cta::kPetNameLen]) return false;
    out = p;
    return true;
}
bool savePet(const cta::PetConfig& p) { return saveBytes("pet", &p, sizeof(p)); }
bool loadSettings(cta::DeviceSettings& out) { return loadBytes("settings", &out, sizeof(out)); }
bool saveSettings(const cta::DeviceSettings& s) { return saveBytes("settings", &s, sizeof(s)); }

void wipeLink() {
    Preferences p;
    if (!p.begin(kNs, false)) return;
    p.remove("wifi");
    p.remove("token");
    p.end();
}

}  // namespace store
