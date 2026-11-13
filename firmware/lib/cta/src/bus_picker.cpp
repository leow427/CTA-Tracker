#include "bus_picker.h"

#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

namespace {

// Parses bustime-response.<list> into doc (big lists go to the json_heap allocator). False on malformed JSON or
// when there's no list (an error reply).
template <typename Fields>
bool parseList(const char* json, size_t len, const char* list, Fields fields, JsonDocument& doc) {
    if (json == nullptr) return false;
    JsonDocument asList, asObject;
    fields(asList["bustime-response"][list][0].to<JsonObject>());
    fields(asObject["bustime-response"][list].to<JsonObject>());
    if (!json::parseShapes(json, len, doc, {&asList, &asObject}, [&](JsonDocument& d) {
            JsonVariantConst v = d["bustime-response"][list];
            return v.is<JsonArrayConst>() || v.is<JsonObjectConst>();
        }))
        return false;
    JsonVariantConst v = doc["bustime-response"][list];
    return v.is<JsonArrayConst>() || v.is<JsonObjectConst>();
}

}  // namespace

int parseBusRoutes(const char* json, size_t len, BusRoute* out, size_t max) {
    JsonDocument doc(json::bigAllocator());
    if (!parseList(json, len, "routes", [](JsonObject f) { f["rt"] = true; f["rtnm"] = true; }, doc)) return -1;
    size_t n = 0;
    json::forEachItem(doc["bustime-response"]["routes"], [&](JsonObjectConst r) {
        const char* rt = r["rt"];
        if (n >= max || rt == nullptr || !*rt) return;
        copyTruncated(out[n].rt, rt, sizeof(out[n].rt) - 1);
        copyTruncated(out[n].name, r["rtnm"] | "", kNameLen);
        ++n;
    });
    return static_cast<int>(n);
}

int parseBusDirections(const char* json, size_t len, BusDirection* out, size_t max) {
    JsonDocument doc;
    if (!parseList(json, len, "directions", [](JsonObject f) { f["dir"] = true; f["id"] = true; }, doc)) return -1;
    size_t n = 0;
    json::forEachItem(doc["bustime-response"]["directions"], [&](JsonObjectConst d) {
        const char* name = d["dir"].as<const char*>();
        if (name == nullptr) name = d["id"].as<const char*>();
        if (n >= max || name == nullptr || !*name) return;
        copyTruncated(out[n++].name, name, sizeof(out[0].name) - 1);
    });
    return static_cast<int>(n);
}

int parseBusStops(const char* json, size_t len, BusStop* out, size_t max) {
    JsonDocument doc(json::bigAllocator());
    if (!parseList(json, len, "stops",
                   [](JsonObject f) { f["stpid"] = true; f["stpnm"] = true; f["lat"] = true; f["lon"] = true; }, doc))
        return -1;
    size_t n = 0;
    json::forEachItem(doc["bustime-response"]["stops"], [&](JsonObjectConst s) {
        const char* id = s["stpid"];
        if (n >= max || id == nullptr || !*id) return;
        copyTruncated(out[n].id, id, sizeof(out[n].id) - 1);
        copyTruncated(out[n].name, s["stpnm"] | "", kNameLen);
        out[n].lat = s["lat"] | 0.f;
        out[n].lon = s["lon"] | 0.f;
        ++n;
    });
    return static_cast<int>(n);
}

bool pickerListFor(const char* listRoute, const char* listDir, const char* route, const char* dir) {
    return strcmp(listRoute, route) == 0 && strcmp(listDir, dir) == 0;
}

}  // namespace cta
