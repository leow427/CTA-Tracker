#include "stop_record.h"

#include "json_util.h"
#include <string.h>

#include "text.h"

namespace cta {

size_t encodeStopRecord(const StopRecord& r, char* buf, size_t cap) {
    Doc doc;
    doc["id"] = r.id;
    doc["type"] = r.type == Kind::Bus ? "bus" : "train";
    doc["route"] = r.route;
    doc["dir"] = r.dir;
    doc["name"] = r.name;
    JsonArray arr = doc["arrivals"].to<JsonArray>();
    for (uint8_t i = 0; i < r.count && i < kMaxArrivals; i++) {
        JsonObject a = arr.add<JsonObject>();
        a["min"] = r.arrivals[i].min;
        a["dest"] = r.arrivals[i].dest;
        a["live"] = r.arrivals[i].live;
        a["delayed"] = r.arrivals[i].delayed;
    }
    doc["alert"] = r.alert;
    doc["ts"] = r.ts;
    size_t need = measureJson(doc);
    if (need + 1 > cap) return 0;
    return serializeJson(doc, buf, cap);
}

bool decodeStopRecord(const char* json, size_t len, StopRecord& out) {
    Doc doc;
    if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) return false;
    const char* id = doc["id"];
    if (!id || !*id) return false;
    StopRecord r{};
    copyTruncated(r.id, id, kIdLen);
    r.type = strcmp(doc["type"] | "train", "bus") == 0 ? Kind::Bus : Kind::Train;
    copyTruncated(r.route, doc["route"] | "", kIdLen);
    copyTruncated(r.dir, doc["dir"] | "", kNameLen);
    copyTruncated(r.name, doc["name"] | "", kNameLen);
    for (JsonObject a : doc["arrivals"].as<JsonArray>()) {
        if (r.count == kMaxArrivals) break;
        Arrival& dst = r.arrivals[r.count++];
        int min = a["min"] | 0;
        dst.min = (int16_t)(min < 0 ? 0 : min > 999 ? 999 : min);
        copyTruncated(dst.dest, a["dest"] | "", kNameLen);
        dst.live = a["live"] | false;
        dst.delayed = a["delayed"] | false;
    }
    r.alert = doc["alert"] | false;
    r.ts = doc["ts"] | 0u;
    out = r;
    return true;
}

}  // namespace cta
