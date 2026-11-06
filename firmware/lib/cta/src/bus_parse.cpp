#include "bus_parse.h"

#include <string.h>

#include <initializer_list>

#include "arrivals_sort.h"
#include "json_util.h"
#include "local_time.h"
#include "text.h"

namespace cta {

// Parses `key`'s list, keeping `fields` of each item (all of it when fields is empty). Returns 1 when the
// list has items, 0 when it's missing and every error names a stop, route or pattern, -1 otherwise.
static int busBody(const char* json, size_t len, JsonDocument& doc, const char* key,
                   std::initializer_list<const char*> fields) {
    auto build = [&](JsonDocument& f, bool many) {
        f["bustime-response"]["error"] = true;
        JsonVariant item = listItem(f["bustime-response"][key], many);
        if (fields.size() == 0) item.set(true);
        for (const char* k : fields) item[k] = true;
    };
    if (deserializeList(doc, json, len, build, [&](JsonDocument& d) { return d["bustime-response"][key]; }))
        return -1;
    JsonVariantConst body = doc["bustime-response"];
    if (!body.is<JsonObjectConst>()) return -1;
    if (!body[key].isNull()) return 1;
    bool fatal = false;
    forEachItem(body["error"], [&](JsonVariantConst err) {
        if (err["stpid"].isNull() && err["rt"].isNull() && err["pid"].isNull()) fatal = true;
    });
    return fatal ? -1 : 0;
}

int parseBusPredictions(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord* out,
                        size_t max) {
    Doc doc;
    int st = busBody(json, len, doc, "prd", {"tmstmp", "stpid", "stpnm", "rt", "rtdir", "des", "prdtm", "prdctdn", "dly"});
    if (st <= 0) return st;

    struct Acc { SoonestArrivals s; char route[kMaxArrivals][kIdLen + 1]; char dir[kMaxArrivals][kNameLen + 1]; };
    static_assert(sizeof(Acc) < 1024, "accumulator stays small");
    Acc acc[10]{};
    size_t n = 0, cap = max < 10 ? max : 10;
    forEachItem(doc["bustime-response"]["prd"], [&](JsonVariantConst pr) {
        const char* stpid = pr["stpid"];
        const char* rt = pr["rt"] | "";
        if (!stpid || (routeFilter && !sameRoute(rt, routeFilter))) return;
        // Time it first: a prediction whose time doesn't parse is dropped before it can start a record.
        const char* cd = pr["prdctdn"] | "";
        int64_t secs = 0;
        if (strcmp(cd, "DUE") != 0) {
            int64_t t, ref;
            if (!parseCtaTime(pr["prdtm"], t) || !parseCtaTime(pr["tmstmp"], ref)) return;
            secs = t - ref;
        }
        size_t i = 0;
        while (i < n && strcmp(out[i].id, stpid) != 0) i++;
        if (i == n) {
            if (n == cap) return;
            out[i] = StopRecord{};
            copyTruncated(out[i].id, stpid, kIdLen);
            copyTruncated(out[i].name, pr["stpnm"] | "", kNameLen);
            out[i].type = Kind::Bus;
            acc[i].s.count = 0;
            n++;
        }
        Arrival a{};
        int64_t min = secs < 0 ? 0 : secs / 60;
        a.min = (int16_t)(min > 999 ? 999 : min);
        copyTruncated(a.dest, pr["des"] | "", kNameLen);
        a.live = true;
        a.delayed = (pr["dly"] | false) || strcmp(cd, "DLY") == 0;
        int pos = acc[i].s.add(secs, a);
        if (pos < 0) return;
        for (int k = acc[i].s.count - 1; k > pos; k--) {
            strcpy(acc[i].route[k], acc[i].route[k - 1]);
            strcpy(acc[i].dir[k], acc[i].dir[k - 1]);
        }
        copyTruncated(acc[i].route[pos], rt, kIdLen);
        copyTruncated(acc[i].dir[pos], pr["rtdir"] | "", kNameLen);
    });
    for (size_t i = 0; i < n; i++) {
        out[i].count = acc[i].s.count;
        memcpy(out[i].arrivals, acc[i].s.items, sizeof(Arrival) * acc[i].s.count);
        strcpy(out[i].route, acc[i].route[0]);
        strcpy(out[i].dir, acc[i].dir[0]);
        out[i].ts = now;
    }
    return (int)n;
}

int parseBusVehicles(const char* json, size_t len, BusVehicle* out, size_t max) {
    Doc doc;
    int st = busBody(json, len, doc, "vehicle", {"pid", "pdist"});
    if (st <= 0) return st;
    size_t n = 0;
    forEachItem(doc["bustime-response"]["vehicle"], [&](JsonVariantConst v) {
        long pid, pdist;
        if (n >= max || !looseLong(v["pid"], pid) || !looseLong(v["pdist"], pdist)) return;
        out[n++] = BusVehicle{(int32_t)pid, (int32_t)pdist};
    });
    return (int)n;
}

int parseBusRoutes(const char* json, size_t len, BusRoute* out, size_t max) {
    Doc doc;
    int st = busBody(json, len, doc, "routes", {"rt", "rtnm"});
    if (st <= 0) return st;
    size_t n = 0;
    forEachItem(doc["bustime-response"]["routes"], [&](JsonVariantConst r) {
        const char* rt = r["rt"];
        if (!rt || n >= max) return;
        copyTruncated(out[n].rt, rt, kIdLen);
        copyTruncated(out[n].name, r["rtnm"] | "", kNameLen);
        n++;
    });
    return (int)n;
}

int parseBusDirections(const char* json, size_t len, char (*out)[kNameLen + 1], size_t max) {
    Doc doc;
    int st = busBody(json, len, doc, "directions", {"dir", "id"});   // v2 names it dir, v3 id
    if (st <= 0) return st;
    size_t n = 0;
    forEachItem(doc["bustime-response"]["directions"], [&](JsonVariantConst d) {
        const char* name = d["dir"] | (d["id"] | (const char*)nullptr);
        if (!name || n >= max) return;
        copyTruncated(out[n++], name, kNameLen);
    });
    return (int)n;
}

int parseBusStops(const char* json, size_t len, BusStop* out, size_t max) {
    Doc doc;
    int st = busBody(json, len, doc, "stops", {"stpid", "stpnm"});
    if (st <= 0) return st;
    size_t n = 0;
    forEachItem(doc["bustime-response"]["stops"], [&](JsonVariantConst s) {
        const char* id = s["stpid"];
        if (!id || n >= max) return;
        copyTruncated(out[n].id, id, kIdLen);
        copyTruncated(out[n].name, s["stpnm"] | "", kNameLen);
        n++;
    });
    return (int)n;
}

}  // namespace cta
