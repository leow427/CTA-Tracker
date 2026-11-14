#include "link_msg.h"

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

namespace cta {

namespace {

// Parses a body that must be a JSON object. False for null, oversize, malformed, or another type.
bool parseObject(const char* json, size_t len, JsonDocument& doc) {
    if (json == nullptr || len > kMaxBody) return false;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    return doc.is<JsonObjectConst>();
}

// Copies a string of minLen–maxLen bytes into dst (maxLen + 1 bytes). False if it's missing, not a string, or
// out of range.
bool copyExact(char* dst, JsonVariantConst v, size_t minLen, size_t maxLen) {
    const char* s = v.as<const char*>();
    if (s == nullptr) return false;
    const size_t n = strlen(s);
    if (n < minLen || n > maxLen) return false;
    memcpy(dst, s, n + 1);
    return true;
}

// Copies a string, cut to maxLen bytes on a UTF-8 boundary. False if it's missing or not a string.
bool copyCut(char* dst, JsonVariantConst v, size_t maxLen) {
    const char* s = v.as<const char*>();
    if (s == nullptr) return false;
    copyTruncated(dst, s, maxLen);
    return true;
}

bool isLowerHex(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

bool validToken(const char* t) { return t != nullptr && strlen(t) == kTokenLen && isLowerHex(t, kTokenLen); }

bool readStop(JsonObjectConst o, PickedStop& p) {
    memset(&p, 0, sizeof(p));
    const char* type = o["type"] | "";
    if (strcmp(type, "train") == 0) p.type = Kind::Train;
    else if (strcmp(type, "bus") == 0) p.type = Kind::Bus;
    else return false;
    return copyExact(p.route, o["route"], 1, sizeof(p.route) - 1) && copyExact(p.id, o["id"], 1, sizeof(p.id) - 1) &&
           copyCut(p.dir, o["dir"], sizeof(p.dir) - 1) && copyCut(p.name, o["name"], kNameLen);
}

template <typename T>
bool intIn(JsonVariantConst v, long lo, long hi, T& out) {
    if (!v.is<long>()) return false;
    const long n = v.as<long>();
    if (n < lo || n > hi) return false;
    out = static_cast<T>(n);
    return true;
}

bool readStops(JsonVariantConst v, PickedStop out[kDockStops]) {
    if (!v.is<JsonArrayConst>()) return false;
    // decodePicks takes text: re-serialize the list (it came from a body of at most kMaxBody bytes).
    const size_t n = measureJson(v);
    char* buf = static_cast<char*>(malloc(n + 1));
    if (buf == nullptr) return false;
    serializeJson(v, buf, n + 1);
    const bool ok = decodePicks(buf, n, out);
    free(buf);
    return ok;
}

bool readPet(JsonVariantConst v, PetConfig& pet) {
    if (!copyCut(pet.name, v["name"], kPetNameLen)) return false;
    JsonArrayConst colors = v["colors"];
    if (colors.isNull() || colors.size() != 2) return false;
    for (int i = 0; i < 2; ++i)
        if (!intIn(colors[i], 0, 0xFFFFFF, pet.colors[i])) return false;
    return true;
}

bool readSettings(JsonVariantConst v, DeviceSettings& s) {
    return intIn(v["screen"], 0, 100, s.screen) && intIn(v["led"], 0, 100, s.led) &&
           intIn(v["night_start"], 0, 1439, s.nightStart) && intIn(v["night_end"], 0, 1439, s.nightEnd);
}

size_t finish(const JsonDocument& doc, char* buf, size_t cap) {
    if (buf == nullptr || measureJson(doc) + 1 > cap) return 0;
    return serializeJson(doc, buf, cap);
}

}  // namespace

bool parseWifi(const char* json, size_t len, uint8_t maxSlots, WifiMsg& out) {
    JsonDocument doc;
    if (!parseObject(json, len, doc)) return false;
    WifiMsg w;
    if (!intIn(doc["slot"], 0, static_cast<long>(maxSlots) - 1, w.slot)) return false;
    if (!copyExact(w.ssid, doc["ssid"], 1, kSsidLen) || !copyExact(w.pass, doc["pass"], 0, kPassLen)) return false;
    out = w;
    return true;
}

bool parseToken(const char* json, size_t len, char out[kTokenLen + 1]) {
    JsonDocument doc;
    if (!parseObject(json, len, doc)) return false;
    const char* t = doc["token"];
    if (!validToken(t)) return false;
    memcpy(out, t, kTokenLen + 1);
    return true;
}

bool tokenMatches(const char* authHeader, const char* token) {
    static const char kBearer[] = "Bearer ";
    constexpr size_t kBearerLen = sizeof(kBearer) - 1;
    if (!validToken(token) || authHeader == nullptr || strncmp(authHeader, kBearer, kBearerLen) != 0) return false;
    const char* given = authHeader + kBearerLen;
    if (strlen(given) != kTokenLen) return false;
    uint8_t diff = 0;   // no early exit: the time doesn't depend on how much of the token matched
    for (size_t i = 0; i < kTokenLen; ++i) diff |= static_cast<uint8_t>(given[i] ^ token[i]);
    return diff == 0;
}

bool parseStop(const char* json, size_t len, PickedStop& out) {
    JsonDocument doc;
    if (!parseObject(json, len, doc)) return false;
    PickedStop p;
    if (!readStop(doc.as<JsonObjectConst>(), p)) return false;
    out = p;
    return true;
}

bool parseFollow(const char* json, size_t len, FollowMsg& out) {
    JsonDocument doc;
    if (!parseObject(json, len, doc)) return false;
    FollowMsg f;
    memset(&f, 0, sizeof(f));
    if (!readStop(doc.as<JsonObjectConst>(), f.stop) || !copyExact(f.vehicle, doc["vehicle"], 1, kVehicleLen) ||
        !copyCut(f.dest, doc["dest"], kNameLen))
        return false;
    out = f;
    return true;
}

bool parseConfig(const char* json, size_t len, ConfigMsg& out) {
    JsonDocument doc;
    if (!parseObject(json, len, doc)) return false;
    ConfigMsg c;
    memset(c.stops, 0, sizeof(c.stops));
    memset(&c.pet, 0, sizeof(c.pet));
    memset(&c.settings, 0, sizeof(c.settings));
    JsonVariantConst stops = doc["stops"], pet = doc["pet"], settings = doc["settings"];
    if (!stops.isNull() && !(c.hasStops = readStops(stops, c.stops))) return false;
    if (!pet.isNull() && !(c.hasPet = readPet(pet, c.pet))) return false;
    if (!settings.isNull() && !(c.hasSettings = readSettings(settings, c.settings))) return false;
    out = c;
    return true;
}

size_t encodeConfig(const ConfigMsg& in, char* buf, size_t cap) {
    JsonDocument doc;
    doc.to<JsonObject>();
    char* picks = nullptr;
    if (in.hasStops) {
        picks = static_cast<char*>(malloc(kMaxBody));
        if (picks == nullptr || encodePicks(in.stops, picks, kMaxBody) == 0) {
            free(picks);
            return 0;
        }
        doc["stops"] = serialized(static_cast<const char*>(picks));
    }
    if (in.hasPet) {
        doc["pet"]["name"] = in.pet.name;
        doc["pet"]["colors"].add(in.pet.colors[0]);
        doc["pet"]["colors"].add(in.pet.colors[1]);
    }
    if (in.hasSettings) {
        JsonObject s = doc["settings"].to<JsonObject>();
        s["screen"] = in.settings.screen;
        s["led"] = in.settings.led;
        s["night_start"] = in.settings.nightStart;
        s["night_end"] = in.settings.nightEnd;
    }
    const size_t n = finish(doc, buf, cap);
    free(picks);
    return n;
}

size_t encodeInfo(const InfoMsg& in, char* buf, size_t cap) {
    JsonDocument doc;
    doc["type"] = in.dock ? "dock" : "handheld";
    doc["id"] = in.id;
    doc["fw"] = in.fw;
    if (in.battery < 0) doc["battery"] = nullptr;
    else doc["battery"] = in.battery;
    doc["charging"] = in.charging;
    doc["wifi"]["connected"] = in.wifiConnected;
    doc["wifi"]["ssid"] = in.ssid;
    doc["last_update"] = in.lastUpdate;
    doc["uptime"] = in.uptime;
    doc["picks_ver"] = in.picksVer;
    doc["wake"] = in.wake;
    doc["docked"] = in.docked;
    return finish(doc, buf, cap);
}

}  // namespace cta
