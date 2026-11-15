#include "nearby_msg.h"

#include <string.h>

#include "json_util.h"

namespace cta {

namespace {

bool digits(const char* s, size_t minLen, size_t maxLen) {
    const size_t n = strlen(s);
    if (n < minLen || n > maxLen) return false;
    for (size_t i = 0; i < n; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

bool isStation(const char* s) { return digits(s, 5, 5) && s[0] == '4'; }
bool isBusStop(const char* s) { return digits(s, 1, Nearby::kIdLen - 1); }

// Copies a list of IDs. False if it isn't a list, is too long, or has a bad or repeated ID.
bool readIds(const cJSON* v, char (*out)[Nearby::kIdLen], int max, int& n, bool (*valid)(const char*)) {
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) > max) return false;
    n = 0;
    for (const cJSON* item = v->child; item != nullptr; item = item->next) {
        const char* id = json::str(item);
        if (id == nullptr || !valid(id)) return false;
        for (int i = 0; i < n; ++i)
            if (strcmp(out[i], id) == 0) return false;
        strcpy(out[n++], id);
    }
    return true;
}

bool listed(const char* id, const char (*list)[Nearby::kIdLen], int n) {
    for (int i = 0; i < n; ++i)
        if (strcmp(list[i], id) == 0) return true;
    return false;
}

}  // namespace

bool parseNearby(const char* json, size_t len, Nearby& out) {
    json::Doc doc(json::parse(json, len));
    const cJSON* lat = json::at(doc.root, "lat");
    const cJSON* lon = json::at(doc.root, "lon");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) return false;
    Nearby n;
    n.lat = lat->valuedouble;
    n.lon = lon->valuedouble;
    if (n.lat < -90 || n.lat > 90 || n.lon < -180 || n.lon > 180) return false;
    if (!readIds(json::at(doc.root, "trains"), n.trains, kNearbyPerTab, n.nTrains, isStation)) return false;
    if (!readIds(json::at(doc.root, "buses"), n.buses, kNearbyPerTab, n.nBuses, isBusStop)) return false;
    if (!readIds(json::at(doc.root, "close"), n.close, kHypedClosestStops, n.nClose,
                 [](const char* s) { return isStation(s) || isBusStop(s); }))
        return false;
    for (int i = 0; i < n.nClose; ++i)
        if (!listed(n.close[i], n.trains, n.nTrains) && !listed(n.close[i], n.buses, n.nBuses)) return false;
    out = n;
    return true;
}

}  // namespace cta
