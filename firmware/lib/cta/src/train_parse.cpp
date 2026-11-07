#include "train_parse.h"

#include <ArduinoJson.h>
#include <string.h>

#include <initializer_list>

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

// Parses with a filter shaped for the common case (lists of objects). CTA sends one object instead of a
// one-item list, which an array filter drops (it comes back null), so `retry` re-parses without a filter when the
// list came back null. Those responses are small: a single arrival, a single train, or none.
template <typename IsListMissing>
bool parseFiltered(const char* json, size_t len, const JsonDocument& filter, JsonDocument& doc,
                   IsListMissing isListMissing) {
    if (json == nullptr) return false;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
        return false;
    if (!doc["ctatt"].is<JsonObjectConst>()) return false;
    if (isListMissing(doc)) {
        doc.clear();
        if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    }
    return true;
}

// Calls f(item) for each element of an array, or once for a lone object.
template <typename F>
void forEachItem(JsonVariantConst v, F f) {
    if (v.is<JsonArrayConst>()) {
        for (JsonVariantConst item : v.as<JsonArrayConst>()) {
            if (item.is<JsonObjectConst>()) f(item.as<JsonObjectConst>());
        }
    } else if (v.is<JsonObjectConst>()) {
        f(v.as<JsonObjectConst>());
    }
}

int16_t minutesFrom(int64_t secs) {
    if (secs <= 0) return 0;
    const int64_t m = secs / 60;
    return m > 32767 ? 32767 : static_cast<int16_t>(m);
}

}  // namespace

bool parseTrainArrivals(const char* json, size_t len, uint32_t now, StopRecord& out) {
    JsonDocument filter;
    filter["ctatt"]["tmst"] = true;
    filter["ctatt"]["errCd"] = true;
    JsonObject f = filter["ctatt"]["eta"][0].to<JsonObject>();
    for (const char* key : {"staNm", "rt", "destNm", "arrT", "isSch", "isDly"}) f[key] = true;

    JsonDocument doc;
    if (!parseFiltered(json, len, filter, doc, [](JsonDocument& d) { return d["ctatt"]["eta"].isNull(); }))
        return false;
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
    forEachItem(etas, [&](JsonObjectConst e) {
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
        a.min = minutesFrom(picks[i].secs);
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
    JsonDocument filter;
    filter["ctatt"]["errCd"] = true;
    filter["ctatt"]["route"][0]["train"][0]["nextStpId"] = true;

    JsonDocument doc;
    // A lone route or a lone train comes back null under the list filter (see parseFiltered).
    auto missing = [](JsonDocument& d) {
        JsonVariantConst routes = d["ctatt"]["route"];
        if (!routes.is<JsonArrayConst>()) return true;
        for (JsonVariantConst r : routes.as<JsonArrayConst>()) {
            if (r["train"].isNull()) return true;
        }
        return false;
    };
    if (!parseFiltered(json, len, filter, doc, missing)) return -1;
    JsonObjectConst body = doc["ctatt"];
    if (!errCdIsZero(body["errCd"])) return -1;

    size_t n = 0;
    forEachItem(body["route"], [&](JsonObjectConst route) {
        forEachItem(route["train"], [&](JsonObjectConst train) {
            if (n >= max) return;
            copyTruncated(out[n].nextStpId, train["nextStpId"].as<const char*>(), sizeof(out[n].nextStpId) - 1);
            ++n;
        });
    });
    return static_cast<int>(n);
}

}  // namespace cta
