#include "train_cards.h"

#include <string.h>

#include "json_util.h"
#include "local_time.h"
#include "text.h"
#include "train_sequences.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::route) - 1;
constexpr size_t kMaxEtas = 64;   // a busy station sends about 40

bool errCdIsZero(const cJSON* v) {
    if (json::str(v)) return strcmp(json::str(v), "0") == 0;
    long n;
    return json::isInt(v, n) && n == 0;
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
    json::Doc doc(json::parse(json, len));
    const cJSON* body = json::at(doc.root, "ctatt");
    if (!cJSON_IsObject(body) || !errCdIsZero(json::at(body, "errCd"))) return -1;

    const cJSON* etas = json::at(body, "eta");
    const bool any = (cJSON_IsArray(etas) && etas->child != nullptr) || cJSON_IsObject(etas);
    const char* tmst = json::str(json::at(body, "tmst"));
    int64_t tmstSecs;
    if (any && !parseCtaTime(tmst, tmstSecs)) return -1;

    // Every arrival by seconds, soonest first; equal times keep response order.
    struct Pick {
        int64_t secs;
        const cJSON* eta;
    };
    static Pick picks[kMaxEtas];
    size_t n = 0;
    json::forEachItem(etas, [&](const cJSON* e) {
        int64_t secs;
        if (!ctaSecondsBetween(tmst, json::str(json::at(e, "arrT")), secs)) return;
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
        const cJSON* e = picks[i].eta;
        const char* stp = json::str(json::at(e, "stpId"));
        const char* rt = json::str(json::at(e, "rt"));
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
            copyTruncated(r.dir, label ? label : json::str(json::at(e, "destNm")), kNameLen);
            copyTruncated(r.name, json::str(json::at(e, "staNm")), kNameLen);
            r.ts = now;
            ++cards;
        }
        StopRecord& r = out[c];
        if (r.count >= kMaxArrivals) continue;
        Arrival& a = r.arrivals[r.count++];
        a.min = json::minutesFrom(picks[i].secs);
        copyTruncated(a.dest, json::str(json::at(e, "destNm")), kNameLen);
        const char* isSch = json::str(json::at(e, "isSch"));
        const char* isDly = json::str(json::at(e, "isDly"));
        a.live = isSch != nullptr && strcmp(isSch, "0") == 0;
        a.delayed = isDly != nullptr && strcmp(isDly, "1") == 0;
        copyTruncated(a.vehicle, json::str(json::at(e, "rn")), kVehicleLen);
    }
    return static_cast<int>(cards);
}

}  // namespace cta
