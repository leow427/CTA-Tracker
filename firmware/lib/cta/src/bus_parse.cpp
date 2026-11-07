#include "bus_parse.h"

#include <ArduinoJson.h>
#include <string.h>

#include <initializer_list>
#include <vector>

#include "json_util.h"
#include "local_time.h"
#include "text.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::id) - 1;

void addErrorFilter(JsonDocument& filter) {
    JsonObject e = filter["bustime-response"]["error"][0].to<JsonObject>();
    for (const char* key : {"stpid", "rt", "pid", "msg"}) e[key] = true;
}

// With no list: 0 if every error names a stop, route, or pattern (nothing there now), −1 otherwise.
int noListResult(JsonVariantConst errors) {
    int result = 0;
    json::forEachItem(errors, [&](JsonObjectConst e) {
        if (e["stpid"].isNull() && e["rt"].isNull() && e["pid"].isNull()) result = -1;
    });
    return result;
}

// Parses a Bus Tracker body whose list lives under `listKey`; returns false if malformed.
bool parseBody(const char* json, size_t len, const char* listKey, JsonDocument& filter, JsonDocument& doc) {
    addErrorFilter(filter);
    // Re-parse unfiltered only when the list isn't a list (a lone item, or none and maybe a lone error): those
    // responses are small. A multi-stop response with both predictions and per-stop errors stays filtered.
    auto missing = [listKey](JsonDocument& d) { return !d["bustime-response"][listKey].is<JsonArrayConst>(); };
    if (!json::parseFiltered(json, len, filter, doc, missing)) return false;
    return doc["bustime-response"].is<JsonObjectConst>();
}

}  // namespace

int parseBusPredictions(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord* out,
                        size_t max) {
    JsonDocument filter;
    JsonObject f = filter["bustime-response"]["prd"][0].to<JsonObject>();
    for (const char* key : {"tmstmp", "stpnm", "stpid", "rt", "rtdir", "des", "prdtm", "prdctdn", "dly"}) f[key] = true;
    JsonDocument doc;
    if (!parseBody(json, len, "prd", filter, doc)) return -1;
    JsonObjectConst body = doc["bustime-response"];
    JsonVariantConst list = body["prd"];
    if (!list.is<JsonArrayConst>() && !list.is<JsonObjectConst>()) return noListResult(body["error"]);

    size_t n = 0;
    std::vector<int64_t> keys(max * kMaxArrivals);  // each kept arrival's sort key, parallel to out[].arrivals
    json::forEachItem(list, [&](JsonObjectConst p) {
        const char* stpid = p["stpid"].as<const char*>();
        const char* rt = p["rt"].as<const char*>();
        if (stpid == nullptr) return;
        if (routeFilter != nullptr && (rt == nullptr || strcmp(rt, routeFilter) != 0)) return;
        const char* countdown = p["prdctdn"].as<const char*>();
        const bool due = countdown != nullptr && strcmp(countdown, "DUE") == 0;
        int64_t secs = 0;
        if (!due && !ctaSecondsBetween(p["tmstmp"].as<const char*>(), p["prdtm"].as<const char*>(), secs)) return;
        const int64_t key = secs < 0 ? 0 : secs;  // past predictions read 0 min, like DUE; ties keep response order

        size_t i = 0;
        char id[kIdLen + 1];
        copyTruncated(id, stpid, kIdLen);
        while (i < n && strcmp(out[i].id, id) != 0) ++i;
        if (i == n) {
            if (n == max) return;
            out[n] = StopRecord{};
            memcpy(out[n].id, id, sizeof(id));
            out[n].type = Kind::Bus;
            out[n].ts = now;
            copyTruncated(out[n].name, p["stpnm"].as<const char*>(), kNameLen);
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
        copyTruncated(a.dest, p["des"].as<const char*>(), kNameLen);
        a.live = true;
        a.delayed = p["dly"].as<bool>() || (countdown != nullptr && strcmp(countdown, "DLY") == 0);
        k[at] = key;
        if (r.count < kMaxArrivals) ++r.count;
        if (at == 0) {
            copyTruncated(r.route, rt, kIdLen);
            copyTruncated(r.dir, p["rtdir"].as<const char*>(), kNameLen);
        }
    });
    return static_cast<int>(n);
}

int parseBusVehicles(const char* json, size_t len, BusVehicle* out, size_t max) {
    JsonDocument filter;
    JsonObject f = filter["bustime-response"]["vehicle"][0].to<JsonObject>();
    f["pid"] = true;
    f["pdist"] = true;
    JsonDocument doc;
    if (!parseBody(json, len, "vehicle", filter, doc)) return -1;
    JsonObjectConst body = doc["bustime-response"];
    JsonVariantConst list = body["vehicle"];
    if (!list.is<JsonArrayConst>() && !list.is<JsonObjectConst>()) return noListResult(body["error"]);

    size_t n = 0;
    json::forEachItem(list, [&](JsonObjectConst v) {
        if (n >= max) return;
        BusVehicle b;
        if (!json::toInt32(v["pid"], b.pid) || !json::toInt32(v["pdist"], b.pdist)) return;
        out[n++] = b;
    });
    return static_cast<int>(n);
}

}  // namespace cta
