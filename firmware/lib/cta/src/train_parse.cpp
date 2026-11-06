#include "train_parse.h"

#include <string.h>

#include <initializer_list>

#include "arrivals_sort.h"
#include "json_util.h"
#include "local_time.h"
#include "text.h"

namespace cta {

static bool ok(const JsonDocument& doc) {
    const char* err = doc["ctatt"]["errCd"];
    return err && strcmp(err, "0") == 0;
}

bool parseTrainArrivals(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord& out) {
    Doc doc;
    auto build = [](JsonDocument& f, bool many) {
        f["ctatt"]["tmst"] = true;
        f["ctatt"]["errCd"] = true;
        JsonVariant eta = listItem(f["ctatt"]["eta"], many);
        for (const char* k : {"staNm", "rt", "destNm", "arrT", "isSch", "isDly"}) eta[k] = true;
    };
    if (deserializeList(doc, json, len, build, [](JsonDocument& d) { return d["ctatt"]["eta"]; }) || !ok(doc))
        return false;

    JsonVariantConst etas = doc["ctatt"]["eta"];
    int64_t tmst = 0;
    bool any = !etas.isNull();
    if (any && !parseCtaTime(doc["ctatt"]["tmst"], tmst)) return false;

    SoonestArrivals soonest;
    char routes[kMaxArrivals][kIdLen + 1];
    const char* station = nullptr;
    forEachItem(etas, [&](JsonVariantConst e) {
        const char* rt = e["rt"] | "";
        if (routeFilter && !sameRoute(rt, routeFilter)) return;
        int64_t at;
        if (!parseCtaTime(e["arrT"], at)) return;
        if (!station) station = e["staNm"];
        int64_t secs = at - tmst;
        Arrival a{};
        int64_t min = secs < 0 ? 0 : secs / 60;
        a.min = (int16_t)(min > 999 ? 999 : min);
        copyTruncated(a.dest, e["destNm"] | "", kNameLen);
        a.live = strcmp(e["isSch"] | "0", "0") == 0;
        a.delayed = strcmp(e["isDly"] | "0", "1") == 0;
        int pos = soonest.add(secs, a);
        if (pos < 0) return;
        for (int i = soonest.count - 1; i > pos; i--) strcpy(routes[i], routes[i - 1]);
        copyTruncated(routes[pos], rt, kIdLen);
    });

    out.type = Kind::Train;
    if (station) copyTruncated(out.name, station, kNameLen);
    out.count = soonest.count;
    memcpy(out.arrivals, soonest.items, sizeof(Arrival) * soonest.count);
    if (soonest.count) strcpy(out.route, routes[0]);
    else if (routeFilter) copyTruncated(out.route, routeFilter, kIdLen);
    out.ts = now;
    return true;
}

int parseTrainPositions(const char* json, size_t len, TrainPos* out, size_t max) {
    Doc doc;
    auto build = [](JsonDocument& f, bool many) {
        f["ctatt"]["errCd"] = true;
        JsonVariant route = listItem(f["ctatt"]["route"], many);
        route["@name"] = true;
        route["train"] = true;   // a route's trains may be one object too
    };
    if (deserializeList(doc, json, len, build, [](JsonDocument& d) { return d["ctatt"]["route"]; }) || !ok(doc))
        return -1;
    size_t n = 0;
    forEachItem(doc["ctatt"]["route"], [&](JsonVariantConst route) {
        const char* line = route["@name"] | "";
        forEachItem(route["train"], [&](JsonVariantConst t) {
            const char* id = t["nextStpId"];
            if (!id || n >= max) return;
            copyTruncated(out[n].nextStpId, id, kIdLen);
            copyTruncated(out[n].route, line, kIdLen);
            n++;
        });
    });
    return (int)n;
}

}  // namespace cta
