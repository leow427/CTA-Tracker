#include "train_parse.h"

#include <ArduinoJson.h>
#include <string.h>

#include <initializer_list>

#include "json_util.h"
#include "local_time.h"
#include "text.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::route) - 1;

bool errCdIsZero(JsonVariantConst v) {
    if (v.is<const char*>()) return strcmp(v.as<const char*>(), "0") == 0;
    if (v.is<long>()) return v.as<long>() == 0;
    return false;
}

void addEtaKeys(JsonObject f) {
    for (const char* key : {"staNm", "rt", "destNm", "arrT", "isSch", "isDly"}) f[key] = true;
}

}  // namespace

bool parseTrainArrivals(const char* json, size_t len, uint32_t now, StopRecord& out) {
    // eta is usually a list; a lone arrival comes as one object, and none as null or missing.
    JsonDocument asList, asObject;
    for (JsonDocument* f : {&asList, &asObject}) {
        (*f)["ctatt"]["tmst"] = true;
        (*f)["ctatt"]["errCd"] = true;
    }
    addEtaKeys(asList["ctatt"]["eta"][0].to<JsonObject>());
    addEtaKeys(asObject["ctatt"]["eta"].to<JsonObject>());

    JsonDocument doc;
    if (!json::parseShapes(json, len, doc, {&asList, &asObject}, [](JsonDocument& d) {
            JsonVariantConst e = d["ctatt"]["eta"];
            return e.is<JsonArrayConst>() || e.is<JsonObjectConst>();
        }))
        return false;
    if (!doc["ctatt"].is<JsonObjectConst>()) return false;
    JsonObjectConst body = doc["ctatt"];
    if (!errCdIsZero(body["errCd"])) return false;

    JsonVariantConst etas = body["eta"];
    const bool any = (etas.is<JsonArrayConst>() && etas.size() > 0) || etas.is<JsonObjectConst>();
    const char* tmst = body["tmst"].as<const char*>();
    int64_t tmstSecs;
    if (any && !parseCtaTime(tmst, tmstSecs)) return false;

    // The soonest kMaxArrivals, by seconds; equal times keep response order.
    struct Pick {
        int64_t secs;
        JsonObjectConst eta;
    } picks[kMaxArrivals];
    size_t n = 0;
    json::forEachItem(etas, [&](JsonObjectConst e) {
        int64_t secs;
        if (!ctaSecondsBetween(tmst, e["arrT"].as<const char*>(), secs)) return;
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
        const JsonObjectConst e = picks[i].eta;
        Arrival& a = r.arrivals[i];
        a.min = json::minutesFrom(picks[i].secs);
        copyTruncated(a.dest, e["destNm"].as<const char*>(), kNameLen);
        const char* isSch = e["isSch"].as<const char*>();
        const char* isDly = e["isDly"].as<const char*>();
        a.live = isSch != nullptr && strcmp(isSch, "0") == 0;
        a.delayed = isDly != nullptr && strcmp(isDly, "1") == 0;
    }
    if (n > 0) {
        copyTruncated(r.name, picks[0].eta["staNm"].as<const char*>(), kNameLen);
        copyTruncated(r.route, picks[0].eta["rt"].as<const char*>(), kIdLen);
    }
    out = r;
    return true;
}

int parseTrainPositions(const char* json, size_t len, TrainPos* out, size_t max) {
    // route is a list of routes, or one route as an object (the dock asks for one line at a time), each with a
    // list of trains. Both shapes stay filtered, since a whole line's trains run to tens of KB; only a lone train
    // object, or no trains, falls through to the unfiltered pass, and those responses are small.
    JsonDocument routeList, routeObject;
    routeList["ctatt"]["errCd"] = true;
    routeList["ctatt"]["route"][0]["train"][0]["nextStpId"] = true;
    routeObject["ctatt"]["errCd"] = true;
    routeObject["ctatt"]["route"]["train"][0]["nextStpId"] = true;
    auto trainsFound = [](JsonDocument& d) {
        JsonVariantConst routes = d["ctatt"]["route"];
        if (routes.is<JsonObjectConst>()) return routes["train"].is<JsonArrayConst>();
        if (!routes.is<JsonArrayConst>()) return false;
        for (JsonVariantConst r : routes.as<JsonArrayConst>()) {
            if (!r["train"].is<JsonArrayConst>()) return false;
        }
        return true;
    };

    JsonDocument doc;
    if (!json::parseShapes(json, len, doc, {&routeList, &routeObject}, trainsFound)) return -1;
    if (!doc["ctatt"].is<JsonObjectConst>()) return -1;
    JsonObjectConst body = doc["ctatt"];
    if (!errCdIsZero(body["errCd"])) return -1;

    size_t n = 0;
    json::forEachItem(body["route"], [&](JsonObjectConst route) {
        json::forEachItem(route["train"], [&](JsonObjectConst train) {
            if (n >= max) return;
            copyTruncated(out[n].nextStpId, train["nextStpId"].as<const char*>(), sizeof(out[n].nextStpId) - 1);
            ++n;
        });
    });
    return static_cast<int>(n);
}

}  // namespace cta
