#include "stop_record.h"

#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::id) - 1;  // ids and routes: 7 bytes
static_assert(sizeof(StopRecord::route) - 1 == kIdLen, "ids and routes share one limit");

// A copy of a record string with control characters (< 0x20) written as spaces: cJSON would write each as a
// six-byte \u00XX, and CTA names never contain them.
struct Clean {
    char s[kNameLen + 1];
    explicit Clean(const char* src) {
        size_t i = 0;
        for (; i < kNameLen && src[i] != '\0'; ++i) {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            s[i] = c < 0x20 ? ' ' : src[i];
        }
        s[i] = '\0';
    }
};

int16_t clampMinutes(long m) {
    if (m < 0) return 0;
    if (m > 32767) return 32767;
    return static_cast<int16_t>(m);
}

size_t encode(const StopRecord& r, char* buf, size_t cap, bool vehicles) {
    const Clean id(r.id), route(r.route), dir(r.dir), name(r.name);
    const uint8_t count = r.count < kMaxArrivals ? r.count : static_cast<uint8_t>(kMaxArrivals);
    const Clean dests[kMaxArrivals] = {Clean(r.arrivals[0].dest), Clean(r.arrivals[1].dest),
                                       Clean(r.arrivals[2].dest), Clean(r.arrivals[3].dest)};
    const Clean vehs[kMaxArrivals] = {Clean(r.arrivals[0].vehicle), Clean(r.arrivals[1].vehicle),
                                      Clean(r.arrivals[2].vehicle), Clean(r.arrivals[3].vehicle)};
    static_assert(kMaxArrivals == 4, "dests and vehs list one Clean per arrival");
    json::Doc doc(cJSON_CreateObject());
    cJSON* root = doc.root;
    bool ok = cJSON_AddStringToObject(root, "id", id.s) &&
              cJSON_AddStringToObject(root, "type", r.type == Kind::Bus ? "bus" : "train") &&
              cJSON_AddStringToObject(root, "route", route.s) && cJSON_AddStringToObject(root, "dir", dir.s) &&
              cJSON_AddStringToObject(root, "name", name.s);
    cJSON* arrivals = ok ? cJSON_AddArrayToObject(root, "arrivals") : nullptr;
    ok = arrivals != nullptr;
    for (uint8_t i = 0; ok && i < count; ++i) {
        cJSON* a = cJSON_CreateObject();
        ok = cJSON_AddItemToArray(arrivals, a);
        if (!ok) {
            cJSON_Delete(a);
            break;
        }
        ok = cJSON_AddNumberToObject(a, "min", r.arrivals[i].min) && cJSON_AddStringToObject(a, "dest", dests[i].s) &&
             cJSON_AddBoolToObject(a, "live", r.arrivals[i].live) &&
             cJSON_AddBoolToObject(a, "delayed", r.arrivals[i].delayed) &&
             (!vehicles || vehs[i].s[0] == '\0' || cJSON_AddStringToObject(a, "veh", vehs[i].s));
    }
    ok = ok && cJSON_AddBoolToObject(root, "alert", r.alert) && cJSON_AddNumberToObject(root, "ts", r.ts);
    return ok ? json::printInto(root, buf, cap, false) : 0;
}

}  // namespace

size_t encodeStopRecord(const StopRecord& r, char* buf, size_t cap) {
    if (buf == nullptr || cap == 0) return 0;
    const size_t len = encode(r, buf, cap, true);
    return len > 0 ? len : encode(r, buf, cap, false);
}

bool decodeStopRecord(const char* json, size_t len, StopRecord& out) {
    json::Doc doc(json::parse(json, len));
    const cJSON* root = doc.root;
    if (!cJSON_IsObject(root) || !cJSON_IsString(json::at(root, "id"))) return false;
    const char* type = json::str(json::at(root, "type"));
    if (type == nullptr || (strcmp(type, "train") != 0 && strcmp(type, "bus") != 0)) return false;

    StopRecord r{};
    copyTruncated(r.id, json::str(json::at(root, "id")), kIdLen);
    r.type = strcmp(type, "bus") == 0 ? Kind::Bus : Kind::Train;
    copyTruncated(r.route, json::str(json::at(root, "route")), kIdLen);
    copyTruncated(r.dir, json::str(json::at(root, "dir")), kNameLen);
    copyTruncated(r.name, json::str(json::at(root, "name")), kNameLen);
    const cJSON* arrivals = json::at(root, "arrivals");
    for (const cJSON* a = cJSON_IsArray(arrivals) ? arrivals->child : nullptr; a != nullptr; a = a->next) {
        if (r.count == kMaxArrivals) break;
        Arrival& dst = r.arrivals[r.count++];
        dst.min = clampMinutes(json::asInt<long>(json::at(a, "min")));
        copyTruncated(dst.dest, json::str(json::at(a, "dest")), kNameLen);
        dst.live = json::asBool(json::at(a, "live"));
        dst.delayed = json::asBool(json::at(a, "delayed"));
        copyTruncated(dst.vehicle, json::str(json::at(a, "veh")), kVehicleLen);
    }
    r.alert = json::asBool(json::at(root, "alert"));
    r.ts = json::asInt<uint32_t>(json::at(root, "ts"));
    out = r;
    return true;
}

}  // namespace cta
