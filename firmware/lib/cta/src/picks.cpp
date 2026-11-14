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
    json::Doc doc(cJSON_CreateArray());
    for (int i = 0; i < kDockStops; ++i) {
        cJSON* o = cJSON_CreateObject();
        if (!cJSON_AddItemToArray(doc.root, o)) {
            cJSON_Delete(o);
            return 0;
        }
        const PickedStop& p = in[i];
        if (!p.id[0]) continue;
        if (!cJSON_AddStringToObject(o, "type", p.type == Kind::Bus ? "bus" : "train") ||
            !cJSON_AddStringToObject(o, "route", p.route) || !cJSON_AddStringToObject(o, "dir", p.dir) ||
            !cJSON_AddStringToObject(o, "id", p.id) || !cJSON_AddStringToObject(o, "name", p.name) ||
            !cJSON_AddStringToObject(o, "rtnm", p.routeName))
            return 0;
    }
    return json::printInto(doc.root, buf, cap, true);
}

bool decodePicks(const char* json, size_t len, PickedStop out[kDockStops]) {
    json::Doc doc(json::parse(json, len));
    if (!cJSON_IsArray(doc.root) || cJSON_GetArraySize(doc.root) != kDockStops) return false;
    PickedStop got[kDockStops];
    const cJSON* v = doc.root->child;
    for (int i = 0; i < kDockStops; ++i, v = v->next) {
        memset(&got[i], 0, sizeof(got[i]));
        if (cJSON_IsNull(v)) continue;
        if (!cJSON_IsObject(v)) return false;
        const char* id = json::str(json::at(v, "id"));
        if (id == nullptr || !*id) continue;
        const char* type = json::strOr(json::at(v, "type"), "");
        if (strcmp(type, "train") != 0 && strcmp(type, "bus") != 0) return false;
        got[i] = make(strcmp(type, "bus") == 0 ? Kind::Bus : Kind::Train, json::strOr(json::at(v, "route"), ""),
                      json::strOr(json::at(v, "dir"), ""), id, json::strOr(json::at(v, "name"), ""),
                      json::strOr(json::at(v, "rtnm"), ""));
    }
    memcpy(out, got, sizeof(got));
    return true;
}

}  // namespace cta
