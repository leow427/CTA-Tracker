#include "picks.h"

#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

namespace {

PickedStop make(Kind k, const char* route, const char* dir, const char* id, const char* name, const char* rtnm) {
    PickedStop p;
    memset(&p, 0, sizeof(p));
    p.type = k;
    copyTruncated(p.route, route, sizeof(p.route) - 1);
    copyTruncated(p.dir, dir, sizeof(p.dir) - 1);
    copyTruncated(p.id, id, sizeof(p.id) - 1);
    copyTruncated(p.name, name, kNameLen);
    copyTruncated(p.routeName, rtnm, kNameLen);
    return p;
}

}  // namespace

bool samePick(const PickedStop& a, const PickedStop& b) {
    if (!a.id[0] || !b.id[0]) return !a.id[0] && !b.id[0];
    return a.type == b.type && strcmp(a.route, b.route) == 0 && strcmp(a.id, b.id) == 0 && strcmp(a.dir, b.dir) == 0;
}

void defaultPicks(PickedStop out[kDockStops]) {
    out[0] = make(Kind::Train, "Red", "95th/Dan Ryan", "30256", "Belmont", "");
    out[1] = make(Kind::Train, "Brn", "Loop", "30104", "Diversey", "");
    out[2] = make(Kind::Bus, "22", "Southbound", "1828", "Clark & Belmont", "Clark");
}

size_t encodePicks(const PickedStop in[kDockStops], char* buf, size_t cap) {
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    for (int i = 0; i < kDockStops; ++i) {
        JsonObject o = list.add<JsonObject>();
        const PickedStop& p = in[i];
        if (!p.id[0]) continue;
        o["type"] = p.type == Kind::Bus ? "bus" : "train";
        o["route"] = p.route;
        o["dir"] = p.dir;
        o["id"] = p.id;
        o["name"] = p.name;
        o["rtnm"] = p.routeName;
    }
    const size_t need = measureJson(doc);
    if (buf == nullptr || need + 1 > cap) return 0;
    return serializeJson(doc, buf, cap);
}

bool decodePicks(const char* json, size_t len, PickedStop out[kDockStops]) {
    if (json == nullptr) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    JsonArrayConst list = doc.as<JsonArrayConst>();
    if (list.isNull() || list.size() != kDockStops) return false;
    PickedStop got[kDockStops];
    for (int i = 0; i < kDockStops; ++i) {
        JsonVariantConst v = list[i];
        memset(&got[i], 0, sizeof(got[i]));
        if (v.isNull()) continue;
        if (!v.is<JsonObjectConst>()) return false;
        const char* id = v["id"];
        if (id == nullptr || !*id) continue;
        const char* type = v["type"] | "";
        if (strcmp(type, "train") != 0 && strcmp(type, "bus") != 0) return false;
        got[i] = make(strcmp(type, "bus") == 0 ? Kind::Bus : Kind::Train, v["route"] | "", v["dir"] | "", id,
                      v["name"] | "", v["rtnm"] | "");
    }
    memcpy(out, got, sizeof(got));
    return true;
}

}  // namespace cta
