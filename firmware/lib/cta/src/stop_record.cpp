#include "stop_record.h"

#include <ArduinoJson.h>
#include <string.h>

#include "text.h"

namespace cta {

namespace {

constexpr size_t kIdLen = sizeof(StopRecord::id) - 1;  // ids and routes: 7 bytes
static_assert(sizeof(StopRecord::route) - 1 == kIdLen, "ids and routes share one limit");

// An ArduinoJson writer into a fixed buffer that never writes past cap (serializeJson's char* overload always
// reserves a byte for the NUL, which would make an exactly-512-byte record not fit).
class BoundedWriter {
public:
    BoundedWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {}
    size_t write(uint8_t c) {
        if (n_ >= cap_) return 0;
        buf_[n_++] = static_cast<char>(c);
        return 1;
    }
    size_t write(const uint8_t* s, size_t len) {
        size_t i = 0;
        while (i < len && write(s[i])) ++i;
        return i;
    }

private:
    char* buf_;
    size_t cap_;
    size_t n_ = 0;
};

// A copy of a record string with control characters (< 0x20) written as spaces: ArduinoJson writes them raw,
// which isn't valid JSON, and CTA names never contain them.
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

}  // namespace

size_t encodeStopRecord(const StopRecord& r, char* buf, size_t cap) {
    if (buf == nullptr || cap == 0) return 0;
    // The cleaned copies live until serialization ends, so the document may keep pointers to them.
    const Clean id(r.id), route(r.route), dir(r.dir), name(r.name);
    const uint8_t count = r.count < kMaxArrivals ? r.count : static_cast<uint8_t>(kMaxArrivals);
    const Clean dests[kMaxArrivals] = {Clean(r.arrivals[0].dest), Clean(r.arrivals[1].dest),
                                       Clean(r.arrivals[2].dest), Clean(r.arrivals[3].dest)};
    static_assert(kMaxArrivals == 4, "dests lists one Clean per arrival");
    JsonDocument doc;
    doc["id"] = id.s;
    doc["type"] = r.type == Kind::Bus ? "bus" : "train";
    doc["route"] = route.s;
    doc["dir"] = dir.s;
    doc["name"] = name.s;
    JsonArray arrivals = doc["arrivals"].to<JsonArray>();
    for (uint8_t i = 0; i < count; ++i) {
        JsonObject a = arrivals.add<JsonObject>();
        a["min"] = r.arrivals[i].min;
        a["dest"] = dests[i].s;
        a["live"] = r.arrivals[i].live;
        a["delayed"] = r.arrivals[i].delayed;
    }
    doc["alert"] = r.alert;
    doc["ts"] = r.ts;
    if (doc.overflowed()) return 0;
    const size_t len = measureJson(doc);
    if (len > cap) return 0;
    BoundedWriter w(buf, cap);
    if (serializeJson(doc, w) != len) return 0;
    if (len < cap) buf[len] = '\0';
    return len;
}

bool decodeStopRecord(const char* json, size_t len, StopRecord& out) {
    if (json == nullptr) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (root.isNull() || !root["id"].is<const char*>()) return false;
    const char* type = root["type"].as<const char*>();
    if (type == nullptr || (strcmp(type, "train") != 0 && strcmp(type, "bus") != 0)) return false;

    StopRecord r{};
    copyTruncated(r.id, root["id"].as<const char*>(), kIdLen);
    r.type = strcmp(type, "bus") == 0 ? Kind::Bus : Kind::Train;
    copyTruncated(r.route, root["route"].as<const char*>(), kIdLen);
    copyTruncated(r.dir, root["dir"].as<const char*>(), kNameLen);
    copyTruncated(r.name, root["name"].as<const char*>(), kNameLen);
    for (JsonObjectConst a : root["arrivals"].as<JsonArrayConst>()) {
        if (r.count == kMaxArrivals) break;
        Arrival& dst = r.arrivals[r.count++];
        dst.min = clampMinutes(a["min"].as<long>());
        copyTruncated(dst.dest, a["dest"].as<const char*>(), kNameLen);
        dst.live = a["live"].as<bool>();
        dst.delayed = a["delayed"].as<bool>();
    }
    r.alert = root["alert"].as<bool>();
    r.ts = root["ts"].as<uint32_t>();
    out = r;
    return true;
}

}  // namespace cta
