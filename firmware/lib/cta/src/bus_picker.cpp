#include "bus_picker.h"

#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

namespace {

// bustime-response.<list> (a list, or one item as an object), or nullptr on malformed JSON or when there's no list
// (an error reply).
const cJSON* listOf(const json::Doc& doc, const char* list) {
    const cJSON* v = json::at(json::at(doc.root, "bustime-response"), list);
    return cJSON_IsArray(v) || cJSON_IsObject(v) ? v : nullptr;
}

}  // namespace

int parseBusRoutes(const char* json, size_t len, BusRoute* out, size_t max) {
    json::Doc doc(json::parse(json, len));
    const cJSON* list = listOf(doc, "routes");
    if (list == nullptr) return -1;
    size_t n = 0;
    json::forEachItem(list, [&](const cJSON* r) {
        const char* rt = json::str(json::at(r, "rt"));
        if (n >= max || rt == nullptr || !*rt) return;
        copyTruncated(out[n].rt, rt, sizeof(out[n].rt) - 1);
        copyTruncated(out[n].name, json::strOr(json::at(r, "rtnm"), ""), kNameLen);
        ++n;
    });
    return static_cast<int>(n);
}

int parseBusDirections(const char* json, size_t len, BusDirection* out, size_t max) {
    json::Doc doc(json::parse(json, len));
    const cJSON* list = listOf(doc, "directions");
    if (list == nullptr) return -1;
    size_t n = 0;
    json::forEachItem(list, [&](const cJSON* d) {
        const char* name = json::str(json::at(d, "dir"));
        if (name == nullptr) name = json::str(json::at(d, "id"));
        if (n >= max || name == nullptr || !*name) return;
        copyTruncated(out[n++].name, name, sizeof(out[0].name) - 1);
    });
    return static_cast<int>(n);
}

int parseBusStops(const char* json, size_t len, BusStop* out, size_t max) {
    json::Doc doc(json::parse(json, len));
    const cJSON* list = listOf(doc, "stops");
    if (list == nullptr) return -1;
    size_t n = 0;
    json::forEachItem(list, [&](const cJSON* s) {
        const char* id = json::str(json::at(s, "stpid"));
        if (n >= max || id == nullptr || !*id) return;
        copyTruncated(out[n].id, id, sizeof(out[n].id) - 1);
        copyTruncated(out[n].name, json::strOr(json::at(s, "stpnm"), ""), kNameLen);
        out[n].lat = static_cast<float>(json::numOr(json::at(s, "lat"), 0));
        out[n].lon = static_cast<float>(json::numOr(json::at(s, "lon"), 0));
        ++n;
    });
    return static_cast<int>(n);
}

bool pickerListFor(const char* listRoute, const char* listDir, const char* route, const char* dir) {
    return strcmp(listRoute, route) == 0 && strcmp(listDir, dir) == 0;
}

}  // namespace cta
