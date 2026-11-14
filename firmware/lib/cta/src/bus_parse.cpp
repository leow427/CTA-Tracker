#include "bus_parse.h"

#include <string.h>

#include <vector>

#include "json_util.h"
#include "local_time.h"
#include "text.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::id) - 1;

using json::noListResult;

// The bustime-response object of a Bus Tracker body, or nullptr if it's malformed.
const cJSON* bodyOf(const json::Doc& doc) {
    const cJSON* body = json::at(doc.root, "bustime-response");
    return cJSON_IsObject(body) ? body : nullptr;
}

}  // namespace

int parseBusPredictions(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord* out,
                        size_t max) {
    json::Doc doc(json::parse(json, len));
    const cJSON* body = bodyOf(doc);
    if (body == nullptr) return -1;
    const cJSON* list = json::at(body, "prd");
    if (!cJSON_IsArray(list) && !cJSON_IsObject(list)) return noListResult(json::at(body, "error"));

    size_t n = 0;
    std::vector<int64_t> keys(max * kMaxArrivals);  // each kept arrival's sort key, parallel to out[].arrivals
    json::forEachItem(list, [&](const cJSON* p) {
        const char* stpid = json::str(json::at(p, "stpid"));
        const char* rt = json::str(json::at(p, "rt"));
        if (stpid == nullptr || rt == nullptr) return;
        if (routeFilter != nullptr && strcmp(rt, routeFilter) != 0) return;
        const char* countdown = json::str(json::at(p, "prdctdn"));
        const bool due = countdown != nullptr && strcmp(countdown, "DUE") == 0;
        int64_t secs = 0;
        if (!due && !ctaSecondsBetween(json::str(json::at(p, "tmstmp")), json::str(json::at(p, "prdtm")), secs)) return;
        const int64_t key = secs;  // DUE is 0 s, so an already-passed prediction sorts first, as in the app

        // One record per (stop, route, direction): each route at a stop is its own card (spec 6.2), and the few
        // stops that serve a route both ways (mostly terminals) get a card per direction.
        size_t i = 0;
        char id[kIdLen + 1], route[kIdLen + 1], dir[kNameLen + 1];
        copyTruncated(id, stpid, kIdLen);
        copyTruncated(route, rt, kIdLen);
        copyTruncated(dir, json::str(json::at(p, "rtdir")), kNameLen);
        while (i < n && (strcmp(out[i].id, id) != 0 || strcmp(out[i].route, route) != 0 || strcmp(out[i].dir, dir) != 0))
            ++i;
        if (i == n) {
            if (n == max) return;
            out[n] = StopRecord{};
            memcpy(out[n].id, id, sizeof(id));
            memcpy(out[n].route, route, sizeof(route));
            memcpy(out[n].dir, dir, sizeof(dir));
            out[n].type = Kind::Bus;
            out[n].ts = now;
            copyTruncated(out[n].name, json::str(json::at(p, "stpnm")), kNameLen);
            ++n;
        }
        StopRecord& r = out[i];
        int64_t* k = &keys[i * kMaxArrivals];
        size_t at = r.count;
        while (at > 0 && k[at - 1] > key) --at;
        if (at >= kMaxArrivals) return;
        for (size_t j = (r.count < kMaxArrivals ? r.count : kMaxArrivals - 1); j > at; --j) {
            r.arrivals[j] = r.arrivals[j - 1];
            k[j] = k[j - 1];
        }
        Arrival& a = r.arrivals[at];
        a.min = due ? 0 : json::minutesFrom(secs);
        copyTruncated(a.dest, json::str(json::at(p, "des")), kNameLen);
        a.live = true;
        a.delayed = json::asBool(json::at(p, "dly")) || (countdown != nullptr && strcmp(countdown, "DLY") == 0);
        copyTruncated(a.vehicle, json::str(json::at(p, "vid")), kVehicleLen);
        k[at] = key;
        if (r.count < kMaxArrivals) ++r.count;
    });
    return static_cast<int>(n);
}

int parseBusVehicles(const char* json, size_t len, BusVehicle* out, size_t max) {
    json::Doc doc(json::parse(json, len));
    const cJSON* body = bodyOf(doc);
    if (body == nullptr) return -1;
    const cJSON* list = json::at(body, "vehicle");
    if (!cJSON_IsArray(list) && !cJSON_IsObject(list)) return noListResult(json::at(body, "error"));

    size_t n = 0;
    json::forEachItem(list, [&](const cJSON* v) {
        if (n >= max) return;
        BusVehicle b;
        if (!json::toInt32(json::at(v, "pid"), b.pid) || !json::toInt32(json::at(v, "pdist"), b.pdist)) return;
        out[n++] = b;
    });
    return static_cast<int>(n);
}

}  // namespace cta
