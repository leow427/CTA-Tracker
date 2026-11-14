#include "train_parse.h"

#include <string.h>

#include "json_util.h"
#include "local_time.h"
#include "text.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::route) - 1;

bool errCdIsZero(const cJSON* v) {
    if (json::str(v)) return strcmp(json::str(v), "0") == 0;
    long n;
    return json::isInt(v, n) && n == 0;
}

bool sameRouteName(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    for (; *a && *b; ++a, ++b) {
        const char x = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        const char y = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

}  // namespace

bool parseTrainArrivals(const char* json, size_t len, uint32_t now, StopRecord& out, const char* routeFilter) {
    // eta is usually a list; a lone arrival comes as one object, and none as null or missing.
    json::Doc doc(json::parse(json, len));
    const cJSON* body = json::at(doc.root, "ctatt");
    if (!cJSON_IsObject(body) || !errCdIsZero(json::at(body, "errCd"))) return false;

    const cJSON* etas = json::at(body, "eta");
    const bool any = (cJSON_IsArray(etas) && etas->child != nullptr) || cJSON_IsObject(etas);
    const char* tmst = json::str(json::at(body, "tmst"));
    int64_t tmstSecs;
    if (any && !parseCtaTime(tmst, tmstSecs)) return false;

    // The soonest kMaxArrivals, by seconds; equal times keep response order.
    struct Pick {
        int64_t secs;
        const cJSON* eta;
    } picks[kMaxArrivals];
    size_t n = 0;
    json::forEachItem(etas, [&](const cJSON* e) {
        if (routeFilter && !sameRouteName(json::str(json::at(e, "rt")), routeFilter)) return;
        int64_t secs;
        if (!ctaSecondsBetween(tmst, json::str(json::at(e, "arrT")), secs)) return;
        size_t at = n;
        while (at > 0 && picks[at - 1].secs > secs) --at;
        if (at >= kMaxArrivals) return;
        for (size_t k = (n < kMaxArrivals ? n : kMaxArrivals - 1); k > at; --k) picks[k] = picks[k - 1];
        picks[at] = {secs, e};
        if (n < kMaxArrivals) ++n;
    });

    StopRecord r = out;
    r.type = Kind::Train;
    r.ts = now;
    r.count = static_cast<uint8_t>(n);
    for (size_t i = 0; i < n; ++i) {
        const cJSON* e = picks[i].eta;
        Arrival& a = r.arrivals[i];
        a.min = json::minutesFrom(picks[i].secs);
        copyTruncated(a.dest, json::str(json::at(e, "destNm")), kNameLen);
        const char* isSch = json::str(json::at(e, "isSch"));
        const char* isDly = json::str(json::at(e, "isDly"));
        a.live = isSch != nullptr && strcmp(isSch, "0") == 0;
        a.delayed = isDly != nullptr && strcmp(isDly, "1") == 0;
        copyTruncated(a.vehicle, json::str(json::at(e, "rn")), kVehicleLen);
    }
    if (n > 0) {
        copyTruncated(r.name, json::str(json::at(picks[0].eta, "staNm")), kNameLen);
        if (!routeFilter) copyTruncated(r.route, json::str(json::at(picks[0].eta, "rt")), kIdLen);
    }
    out = r;
    return true;
}

int parseTrainPositions(const char* json, size_t len, TrainPos* out, size_t max) {
    // route is a list of routes, or one route as an object (the dock asks for one line at a time), each with a
    // list of trains, or one train as an object.
    json::Doc doc(json::parse(json, len));
    const cJSON* body = json::at(doc.root, "ctatt");
    if (!cJSON_IsObject(body) || !errCdIsZero(json::at(body, "errCd"))) return -1;

    size_t n = 0;
    json::forEachItem(json::at(body, "route"), [&](const cJSON* route) {
        json::forEachItem(json::at(route, "train"), [&](const cJSON* train) {
            if (n >= max) return;
            copyTruncated(out[n].nextStpId, json::str(json::at(train, "nextStpId")), sizeof(out[n].nextStpId) - 1);
            ++n;
        });
    });
    return static_cast<int>(n);
}

}  // namespace cta
