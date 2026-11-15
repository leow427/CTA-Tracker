#include "store.h"

#include <nvs.h>
#include <string.h>

namespace store {

namespace {
// The namespace and keys Arduino's Preferences used: they were plain NVS entries, so what was saved before the
// ESP-IDF port loads unchanged (putBytes = blob, putString = str, putFloat = a 4-byte blob, putUInt = u32).
constexpr const char* kNs = "dock";

// An open handle that closes itself; ok() is false if the namespace couldn't be opened (read-only: not created yet).
struct Nvs {
    nvs_handle_t h = 0;
    bool open;
    explicit Nvs(bool write) : open(nvs_open(kNs, write ? NVS_READWRITE : NVS_READONLY, &h) == ESP_OK) {}
    ~Nvs() { if (open) nvs_close(h); }
};

// A fixed-size value under key; false if it's missing or a different size.
bool loadBytes(const char* key, void* out, size_t n) {
    Nvs p(false);
    size_t len = 0;
    return p.open && nvs_get_blob(p.h, key, nullptr, &len) == ESP_OK && len == n &&
           nvs_get_blob(p.h, key, out, &len) == ESP_OK;
}

bool putBytes(Nvs& p, const char* key, const void* v, size_t n) { return nvs_set_blob(p.h, key, v, n) == ESP_OK; }

// Writes the value unless the saved one is the same.
bool saveBytes(const char* key, const void* v, size_t n) {
    uint8_t old[128];
    if (n <= sizeof(old) && loadBytes(key, old, n) && memcmp(old, v, n) == 0) return true;
    Nvs p(true);
    return p.open && putBytes(p, key, v, n) && nvs_commit(p.h) == ESP_OK;
}

// The saved picks string into out (cap bytes); false if none (or longer than cap).
bool loadPicksText(char* out, size_t cap) {
    Nvs p(false);
    size_t len = cap;
    return p.open && nvs_get_str(p.h, "picks", out, &len) == ESP_OK;
}
}  // namespace

void loadPicks(cta::PickedStop out[cta::kDockStops]) {
    cta::defaultPicks(out);
    char s[640];
    if (!loadPicksText(s, sizeof(s))) return;
    const size_t n = strlen(s);
    if (n && !cta::decodePicks(s, n, out)) cta::defaultPicks(out);
}

bool savePicks(const cta::PickedStop picks[cta::kDockStops], bool* wrote) {
    if (wrote) *wrote = false;
    char buf[640];
    const size_t n = cta::encodePicks(picks, buf, sizeof(buf));
    if (!n) return false;
    // Unchanged picks aren't written again: a flash write upsets the panel (display.h).
    char old[640];
    if (loadPicksText(old, sizeof(old)) && strcmp(old, buf) == 0) return true;
    Nvs p(true);
    const bool ok = p.open && nvs_set_str(p.h, "picks", buf) == ESP_OK && nvs_commit(p.h) == ESP_OK;
    if (wrote) *wrote = p.open;
    return ok;
}

bool loadGeo(float& lat, float& lon, uint32_t& when) {
    float a, o;
    if (!loadBytes("geoLat", &a, sizeof(a)) || !loadBytes("geoLon", &o, sizeof(o))) return false;
    Nvs p(false);
    uint32_t t = 0;
    if (p.open) nvs_get_u32(p.h, "geoT", &t);   // getUInt's default: 0
    lat = a;
    lon = o;
    when = t;
    return true;
}

bool saveGeo(float lat, float lon, uint32_t when) {
    Nvs p(true);
    return p.open && putBytes(p, "geoLat", &lat, sizeof(lat)) && putBytes(p, "geoLon", &lon, sizeof(lon)) &&
           nvs_set_u32(p.h, "geoT", when) == ESP_OK && nvs_commit(p.h) == ESP_OK;
}

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
    Nvs p(true);
    if (!p.open) return;
    nvs_erase_key(p.h, "token");
    nvs_commit(p.h);
}

}  // namespace store
