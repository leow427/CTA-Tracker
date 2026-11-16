#include "ahead.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "json_util.h"
#include "local_time.h"
#include "text.h"
#include "train_map.h"
#include "train_sequences.h"

namespace cta {

namespace {

// Keeps the soonest max stops by seconds, in place (equal times keep reply order). Returns the new count.
size_t insertSoonest(AheadStop* out, int64_t* keys, size_t n, size_t max, const char* name, int64_t secs) {
    size_t at = n;
    while (at > 0 && keys[at - 1] > secs) --at;
    if (at >= max) return n;
    for (size_t k = (n < max ? n : max - 1); k > at; --k) {
        out[k] = out[k - 1];
        keys[k] = keys[k - 1];
    }
    copyTruncated(out[at].name, name, kNameLen);
    out[at].min = json::minutesFrom(secs);
    keys[at] = secs;
    return n < max ? n + 1 : n;
}

// Metres between two points, flat-earth (fine across a station or a street).
float metres(double la0, double lo0, double la1, double lo1) {
    const double kx = 111320.0 * cos(la0 * M_PI / 180), ky = 110540.0;
    const double dx = (lo1 - lo0) * kx, dy = (la1 - la0) * ky;
    return static_cast<float>(sqrt(dx * dx + dy * dy));
}

bool number(const cJSON* v, double& out) {
    if (cJSON_IsNumber(v)) { out = v->valuedouble; return true; }
    const char* s = json::str(v);
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    out = strtod(s, &end);
    return *end == '\0' && isfinite(out);
}

}  // namespace

int parseTrainAhead(const char* json, size_t len, AheadStop* out, size_t max) {
    json::Doc doc(json::parse(json, len));
    const cJSON* body = json::at(doc.root, "ctatt");
    if (!cJSON_IsObject(body)) return -1;
    const cJSON* err = json::at(body, "errCd");
    long code = -1;
    if (const char* s = json::str(err)) code = strtol(s, nullptr, 10);
    else if (!json::isInt(err, code)) return -1;
    if (code == 501) return 0;   // "No trains with runnumber … were found": it has finished its run
    if (code != 0) return -1;
    const char* tmst = json::str(json::at(body, "tmst"));
    int64_t keys[kAheadStops];
    if (max > kAheadStops) max = kAheadStops;
    size_t n = 0;
    json::forEachItem(json::at(body, "eta"), [&](const cJSON* e) {
        int64_t secs;
        if (!ctaSecondsBetween(tmst, json::str(json::at(e, "arrT")), secs)) return;
        n = insertSoonest(out, keys, n, max, json::str(json::at(e, "staNm")), secs);
    });
    return static_cast<int>(n);
}

int parseBusAhead(const char* json, size_t len, AheadStop* out, size_t max) {
    json::Doc doc(json::parse(json, len));
    const cJSON* body = json::at(doc.root, "bustime-response");
    if (!cJSON_IsObject(body)) return -1;
    const cJSON* list = json::at(body, "prd");
    if (!cJSON_IsArray(list) && !cJSON_IsObject(list)) {
        int result = 0;   // every error about the vehicle: it's off the road; any other (a key, the limit) failed
        bool any = false;
        json::forEachItem(json::at(body, "error"), [&](const cJSON* e) {
            any = true;
            if (json::isNull(json::at(e, "vid"))) result = -1;
        });
        return any ? result : -1;
    }
    int64_t keys[kAheadStops];
    if (max > kAheadStops) max = kAheadStops;
    size_t n = 0;
    json::forEachItem(list, [&](const cJSON* p) {
        const char* countdown = json::str(json::at(p, "prdctdn"));
        int64_t secs = 0;
        if (!(countdown && strcmp(countdown, "DUE") == 0) &&
            !ctaSecondsBetween(json::str(json::at(p, "tmstmp")), json::str(json::at(p, "prdtm")), secs))
            return;
        n = insertSoonest(out, keys, n, max, json::str(json::at(p, "stpnm")), secs);
    });
    return static_cast<int>(n);
}

bool oppositeTrainPlatform(const char* route, const char* stopId, const char* dirLabel, PickedStop& out) {
    float la0, lo0;
    if (!route || !stopId || !dirLabel || !trainPlatformLatLon(stopId, la0, lo0)) return false;
    float best = 300;
    bool found = false;
    for (size_t l = 0; l < kTrainLineCount; ++l) {
        const TrainLine& t = kTrainLines[l];
        if (strcmp(t.route, route) != 0 || strcmp(t.dirLabel, dirLabel) == 0) continue;
        for (size_t k = t.lead; k < t.count; ++k) {
            float la, lo;
            if (strcmp(t.stopIds[k], stopId) == 0 || !trainPlatformLatLon(t.stopIds[k], la, lo)) continue;
            const float d = metres(la0, lo0, la, lo);
            if (d >= best) continue;
            best = d;
            found = true;
            out.type = Kind::Train;
            copyTruncated(out.route, route, sizeof(out.route) - 1);
            copyTruncated(out.dir, t.dirLabel, sizeof(out.dir) - 1);
            copyTruncated(out.id, t.stopIds[k], sizeof(out.id) - 1);
            copyTruncated(out.name, t.names[k], kNameLen);
            out.routeName[0] = 0;
        }
    }
    return found;
}

bool oppositeBusStop(const char* json, size_t len, const char* route, const char* stopId, const char* dir,
                     PickedStop& out) {
    if (!route || !stopId || !dir) return false;
    json::Doc doc(json::parse(json, len));
    const cJSON* ptr = json::at(json::at(doc.root, "bustime-response"), "ptr");
    auto stopsOf = [](const cJSON* p, auto f) {
        json::forEachItem(json::at(p, "pt"), [&](const cJSON* pt) {
            double la, lo;
            const char* id = json::str(json::at(pt, "stpid"));
            if (id && strcmp(json::strOr(json::at(pt, "typ"), ""), "S") == 0 && number(json::at(pt, "lat"), la) &&
                number(json::at(pt, "lon"), lo))
                f(pt, id, la, lo);
        });
    };
    bool here = false;
    double la0 = 0, lo0 = 0;
    json::forEachItem(ptr, [&](const cJSON* p) {
        stopsOf(p, [&](const cJSON*, const char* id, double la, double lo) {
            if (!here && strcmp(id, stopId) == 0) { here = true; la0 = la; lo0 = lo; }
        });
    });
    if (!here) return false;
    float best = 500;
    bool found = false;
    json::forEachItem(ptr, [&](const cJSON* p) {
        const char* rtdir = json::str(json::at(p, "rtdir"));
        if (!rtdir || strcmp(rtdir, dir) == 0) return;
        stopsOf(p, [&](const cJSON* pt, const char* id, double la, double lo) {
            const float d = metres(la0, lo0, la, lo);
            if (d >= best) return;
            best = d;
            found = true;
            out.type = Kind::Bus;
            copyTruncated(out.route, route, sizeof(out.route) - 1);
            copyTruncated(out.dir, rtdir, sizeof(out.dir) - 1);
            copyTruncated(out.id, id, sizeof(out.id) - 1);
            copyTruncated(out.name, json::str(json::at(pt, "stpnm")), kNameLen);
        });
    });
    return found;
}

}  // namespace cta
