#include "train_cards.h"

#include <ArduinoJson.h>
#include <string.h>

#include <initializer_list>

#include "json_util.h"
#include "local_time.h"
#include "text.h"
#include "train_sequences.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::route) - 1;
constexpr size_t kMaxEtas = 64;   // a busy station sends about 40

bool errCdIsZero(JsonVariantConst v) {
    if (v.is<const char*>()) return strcmp(v.as<const char*>(), "0") == 0;
    if (v.is<long>()) return v.as<long>() == 0;
    return false;
}

void addEtaKeys(JsonObject f) {
    for (const char* key : {"staNm", "stpId", "rt", "destNm", "arrT", "isSch", "isDly"}) f[key] = true;
}

bool same(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

}  // namespace

const char* trainDirLabel(const char* route, const char* stpId) {
    if (!route || !stpId) return nullptr;
    const char* leadOnly = nullptr;
    for (size_t l = 0; l < kTrainLineCount; ++l) {
        const TrainLine& line = kTrainLines[l];
        if (strcmp(line.route, route) != 0) continue;
        for (uint8_t i = 0; i < line.count; ++i) {
            if (strcmp(line.stopIds[i], stpId) != 0) continue;
            if (i >= line.lead) return line.dirLabel;
            if (!leadOnly) leadOnly = line.dirLabel;
        }
    }
    return leadOnly;
}

int parseTrainCards(const char* json, size_t len, uint32_t now, const char* stationId, StopRecord* out, size_t max) {
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
        return -1;
    if (!doc["ctatt"].is<JsonObjectConst>()) return -1;
    JsonObjectConst body = doc["ctatt"];
    if (!errCdIsZero(body["errCd"])) return -1;

    JsonVariantConst etas = body["eta"];
    const bool any = (etas.is<JsonArrayConst>() && etas.size() > 0) || etas.is<JsonObjectConst>();
    const char* tmst = body["tmst"].as<const char*>();
    int64_t tmstSecs;
    if (any && !parseCtaTime(tmst, tmstSecs)) return -1;

    // Every arrival by seconds, soonest first; equal times keep response order.
    struct Pick {
        int64_t secs;
        JsonObjectConst eta;
    };
    static Pick picks[kMaxEtas];
    size_t n = 0;
    json::forEachItem(etas, [&](JsonObjectConst e) {
        int64_t secs;
        if (!ctaSecondsBetween(tmst, e["arrT"].as<const char*>(), secs)) return;
        size_t at = n;
        while (at > 0 && picks[at - 1].secs > secs) --at;
        if (at >= kMaxEtas) return;
        for (size_t k = (n < kMaxEtas ? n : kMaxEtas - 1); k > at; --k) picks[k] = picks[k - 1];
        picks[at] = {secs, e};
        if (n < kMaxEtas) ++n;
    });

    // Group by (stpId, rt) in order of each card's soonest arrival.
    static const char* keys[kMaxEtas][2];
    size_t cards = 0;
    for (size_t i = 0; i < n; ++i) {
        const JsonObjectConst e = picks[i].eta;
        const char* stp = e["stpId"].as<const char*>();
        const char* rt = e["rt"].as<const char*>();
        if (!stp || !rt) continue;
        size_t c = 0;
        while (c < cards && !(same(keys[c][0], stp) && same(keys[c][1], rt))) ++c;
        if (c == cards) {
            if (cards >= max) continue;
            keys[c][0] = stp;
            keys[c][1] = rt;
            StopRecord& r = out[c];
            memset(&r, 0, sizeof(r));
            copyTruncated(r.id, stationId, sizeof(r.id) - 1);
            r.type = Kind::Train;
            copyTruncated(r.route, rt, kIdLen);
            const char* label = trainDirLabel(rt, stp);
            copyTruncated(r.dir, label ? label : e["destNm"].as<const char*>(), kNameLen);
            copyTruncated(r.name, e["staNm"].as<const char*>(), kNameLen);
            r.ts = now;
            ++cards;
        }
        StopRecord& r = out[c];
        if (r.count >= kMaxArrivals) continue;
        Arrival& a = r.arrivals[r.count++];
        a.min = json::minutesFrom(picks[i].secs);
        copyTruncated(a.dest, e["destNm"].as<const char*>(), kNameLen);
        const char* isSch = e["isSch"].as<const char*>();
        const char* isDly = e["isDly"].as<const char*>();
        a.live = isSch != nullptr && strcmp(isSch, "0") == 0;
        a.delayed = isDly != nullptr && strcmp(isDly, "1") == 0;
    }
    return static_cast<int>(cards);
}

}  // namespace cta
