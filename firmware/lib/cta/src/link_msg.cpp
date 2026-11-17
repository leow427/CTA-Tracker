#include "link_msg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_util.h"
#include "stop_record.h"
#include "text.h"

namespace cta {

namespace {

// Parses a body that must be a JSON object. nullptr for null, oversize, malformed, or another type.
cJSON* parseObject(const char* json, size_t len, size_t maxLen = kMaxBody) {
    if (json == nullptr || len > maxLen) return nullptr;
    cJSON* root = json::parse(json, len);
    if (cJSON_IsObject(root)) return root;
    cJSON_Delete(root);
    return nullptr;
}

// Copies a string of minLen–maxLen bytes into dst (maxLen + 1 bytes). False if it's missing, not a string, or
// out of range.
bool copyExact(char* dst, const cJSON* v, size_t minLen, size_t maxLen) {
    const char* s = json::str(v);
    if (s == nullptr) return false;
    const size_t n = strlen(s);
    if (n < minLen || n > maxLen) return false;
    memcpy(dst, s, n + 1);
    return true;
}

// Copies a string, cut to maxLen bytes on a UTF-8 boundary. False if it's missing or not a string.
bool copyCut(char* dst, const cJSON* v, size_t maxLen) {
    const char* s = json::str(v);
    if (s == nullptr) return false;
    copyTruncated(dst, s, maxLen);
    return true;
}

bool isLowerHex(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

bool readStop(const cJSON* o, PickedStop& p) {
    memset(&p, 0, sizeof(p));
    const char* type = json::strOr(json::at(o, "type"), "");
    if (strcmp(type, "train") == 0) p.type = Kind::Train;
    else if (strcmp(type, "bus") == 0) p.type = Kind::Bus;
    else return false;
    return copyExact(p.route, json::at(o, "route"), 1, sizeof(p.route) - 1) &&
           copyExact(p.id, json::at(o, "id"), 1, sizeof(p.id) - 1) &&
           copyCut(p.dir, json::at(o, "dir"), sizeof(p.dir) - 1) && copyCut(p.name, json::at(o, "name"), kNameLen);
}

template <typename T>
bool intIn(const cJSON* v, long lo, long hi, T& out) {
    long n;
    if (!json::isInt(v, n) || n < lo || n > hi) return false;
    out = static_cast<T>(n);
    return true;
}

bool readStops(const cJSON* v, PickedStop out[kDockStops]) {
    if (!cJSON_IsArray(v)) return false;
    // decodePicks takes text: re-serialize the list (it came from a body of at most kMaxBody bytes).
    char* buf = cJSON_PrintUnformatted(v);
    if (buf == nullptr) return false;
    const bool ok = decodePicks(buf, strlen(buf), out);
    cJSON_free(buf);
    return ok;
}

bool readPet(const cJSON* v, PetConfig& pet) {
    if (!copyCut(pet.name, json::at(v, "name"), kPetNameLen)) return false;
    const cJSON* colors = json::at(v, "colors");
    if (!cJSON_IsArray(colors) || cJSON_GetArraySize(colors) != 2) return false;
    for (int i = 0; i < 2; ++i)
        if (!intIn(cJSON_GetArrayItem(colors, i), 0, 0xFFFFFF, pet.colors[i])) return false;
    return true;
}

bool readSettings(const cJSON* v, DeviceSettings& s) {
    return intIn(json::at(v, "screen"), 0, 100, s.screen) && intIn(json::at(v, "led"), 0, 100, s.led) &&
           intIn(json::at(v, "night_start"), 0, 1439, s.nightStart) &&
           intIn(json::at(v, "night_end"), 0, 1439, s.nightEnd);
}

bool addSettings(cJSON* root, const DeviceSettings& in) {
    cJSON* s = cJSON_AddObjectToObject(root, "settings");
    return cJSON_AddNumberToObject(s, "screen", in.screen) && cJSON_AddNumberToObject(s, "led", in.led) &&
           cJSON_AddNumberToObject(s, "night_start", in.nightStart) &&
           cJSON_AddNumberToObject(s, "night_end", in.nightEnd);
}

}  // namespace

bool validToken(const char* t) { return t != nullptr && strlen(t) == kTokenLen && isLowerHex(t, kTokenLen); }

bool parseWifi(const char* json, size_t len, uint8_t maxSlots, WifiMsg& out) {
    json::Doc doc(parseObject(json, len));
    if (doc.root == nullptr) return false;
    WifiMsg w;
    if (!intIn(json::at(doc.root, "slot"), 0, static_cast<long>(maxSlots) - 1, w.slot)) return false;
    if (!copyExact(w.ssid, json::at(doc.root, "ssid"), 1, kSsidLen) ||
        !copyExact(w.pass, json::at(doc.root, "pass"), 0, kPassLen))
        return false;
    out = w;
    return true;
}

bool parseToken(const char* json, size_t len, char out[kTokenLen + 1]) {
    json::Doc doc(parseObject(json, len));
    const char* t = json::str(json::at(doc.root, "token"));
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
    json::Doc doc(parseObject(json, len));
    if (doc.root == nullptr) return false;
    PickedStop p;
    if (!readStop(doc.root, p)) return false;
    out = p;
    return true;
}

bool parseFollow(const char* json, size_t len, FollowMsg& out) {
    json::Doc doc(parseObject(json, len));
    if (doc.root == nullptr) return false;
    FollowMsg f;
    memset(&f, 0, sizeof(f));
    if (!readStop(doc.root, f.stop) || !copyExact(f.vehicle, json::at(doc.root, "vehicle"), 1, kVehicleLen) ||
        !copyCut(f.dest, json::at(doc.root, "dest"), kNameLen))
        return false;
    out = f;
    return true;
}

bool parseConfig(const char* json, size_t len, ConfigMsg& out) {
    json::Doc doc(parseObject(json, len));
    if (doc.root == nullptr) return false;
    ConfigMsg c;
    memset(c.stops, 0, sizeof(c.stops));
    memset(&c.pet, 0, sizeof(c.pet));
    memset(&c.settings, 0, sizeof(c.settings));
    const cJSON* stops = json::at(doc.root, "stops");
    const cJSON* pet = json::at(doc.root, "pet");
    const cJSON* settings = json::at(doc.root, "settings");
    if (!json::isNull(stops) && !(c.hasStops = readStops(stops, c.stops))) return false;
    if (!json::isNull(pet) && !(c.hasPet = readPet(pet, c.pet))) return false;
    if (!json::isNull(settings) && !(c.hasSettings = readSettings(settings, c.settings))) return false;
    out = c;
    return true;
}

size_t encodeConfig(const ConfigMsg& in, char* buf, size_t cap) {
    json::Doc doc(cJSON_CreateObject());
    cJSON* root = doc.root;
    bool ok = root != nullptr;
    if (ok && in.hasStops) {
        char* picks = static_cast<char*>(malloc(kMaxBody));
        ok = picks != nullptr && encodePicks(in.stops, picks, kMaxBody) != 0 &&
             cJSON_AddRawToObject(root, "stops", picks) != nullptr;
        free(picks);
    }
    if (ok && in.hasPet) {
        cJSON* pet = cJSON_AddObjectToObject(root, "pet");
        cJSON* colors = pet && cJSON_AddStringToObject(pet, "name", in.pet.name) ? cJSON_AddArrayToObject(pet, "colors")
                                                                                 : nullptr;
        for (int i = 0; ok && i < 2; ++i) {
            cJSON* c = colors ? cJSON_CreateNumber(in.pet.colors[i]) : nullptr;
            ok = cJSON_AddItemToArray(colors, c);
            if (!ok) cJSON_Delete(c);
        }
    }
    if (ok && in.hasSettings) ok = addSettings(root, in.settings);
    return ok ? json::printInto(root, buf, cap, true) : 0;
}

size_t encodeInfo(const InfoMsg& in, char* buf, size_t cap) {
    json::Doc doc(cJSON_CreateObject());
    cJSON* root = doc.root;
    cJSON* wifi = nullptr;
    const bool ok =
        cJSON_AddStringToObject(root, "type", in.dock ? "dock" : "handheld") &&
        cJSON_AddStringToObject(root, "id", in.id) && cJSON_AddStringToObject(root, "fw", in.fw) &&
        (in.battery < 0 ? cJSON_AddNullToObject(root, "battery") != nullptr
                        : cJSON_AddNumberToObject(root, "battery", in.battery) != nullptr) &&
        cJSON_AddBoolToObject(root, "charging", in.charging) && (wifi = cJSON_AddObjectToObject(root, "wifi")) &&
        cJSON_AddBoolToObject(wifi, "connected", in.wifiConnected) && cJSON_AddStringToObject(wifi, "ssid", in.ssid) &&
        cJSON_AddNumberToObject(wifi, "rssi", in.rssi) &&
        cJSON_AddNumberToObject(root, "last_update", in.lastUpdate) &&
        cJSON_AddNumberToObject(root, "uptime", in.uptime) && cJSON_AddNumberToObject(root, "picks_ver", in.picksVer) &&
        cJSON_AddNumberToObject(root, "wake", in.wake) && cJSON_AddBoolToObject(root, "docked", in.docked) &&
        cJSON_AddBoolToObject(root, "paired", in.paired) && (!in.hasSettings || addSettings(root, in.settings)) &&
        cJSON_AddBoolToObject(root, "relaying", in.relaying);
    return ok ? json::printInto(root, buf, cap, true) : 0;
}

bool parseEnvelope(const char* msg, size_t len, Method& m, char path[kPathLen + 1], const char*& body,
                   size_t& bodyLen) {
    if (msg == nullptr || len > kMaxBleMsg) return false;
    static const char kPost[] = "POST ", kDelete[] = "DELETE ";
    Method method;
    size_t at;
    if (len >= sizeof(kPost) - 1 && memcmp(msg, kPost, sizeof(kPost) - 1) == 0) {
        method = Method::Post;
        at = sizeof(kPost) - 1;
    } else if (len >= sizeof(kDelete) - 1 && memcmp(msg, kDelete, sizeof(kDelete) - 1) == 0) {
        method = Method::Delete;
        at = sizeof(kDelete) - 1;
    } else {
        return false;
    }
    const char* nl = static_cast<const char*>(memchr(msg + at, '\n', len - at));
    if (nl == nullptr) return false;
    const size_t n = static_cast<size_t>(nl - (msg + at));
    if (n < 2 || n > kPathLen || msg[at] != '/') return false;
    for (size_t i = 0; i < n; ++i)
        if (msg[at + i] <= ' ' || msg[at + i] > '~') return false;
    m = method;
    memcpy(path, msg + at, n);
    path[n] = '\0';
    body = nl + 1;
    bodyLen = len - (static_cast<size_t>(nl - msg) + 1);
    return true;
}

bool parseRelayTick(const char* json, size_t len, RelayTick& out) {
    json::Doc doc(parseObject(json, len));
    if (doc.root == nullptr) return false;
    RelayTick t = {};
    // not intIn: its long bounds are 32 bits on the ESP32, too small for a uint32_t
    if (!json::isInt(json::at(doc.root, "now"), t.now) || t.now <= 1700000000u) return false;
    const cJSON* wmo = json::at(doc.root, "wmo");
    const cJSON* temp = json::at(doc.root, "temp");
    t.hasWeather = !json::isNull(wmo) || !json::isNull(temp);
    if (t.hasWeather) {
        if (!intIn(wmo, 0, 99, t.wmo) || !cJSON_IsNumber(temp)) return false;
        const double c = temp->valuedouble;
        if (!(c >= -100 && c <= 100)) return false;
        t.tempC = static_cast<float>(c);
    }
    out = t;
    return true;
}

bool parseRelayStop(const char* json, size_t len, RelayStop& out) {
    json::Doc doc(parseObject(json, len, kMaxBleMsg));
    if (doc.root == nullptr) return false;
    char id[sizeof(out.id)];
    const char* type = json::strOr(json::at(doc.root, "type"), "");
    const cJSON* records = json::at(doc.root, "records");
    if (!copyExact(id, json::at(doc.root, "id"), 1, sizeof(id) - 1) ||
        (strcmp(type, "train") != 0 && strcmp(type, "bus") != 0) || !cJSON_IsArray(records))
        return false;
    const Kind kind = strcmp(type, "bus") == 0 ? Kind::Bus : Kind::Train;
    // Check every record first, so a bad one leaves out alone without a 3.6 KB temporary on the caller's stack.
    for (const cJSON* r = records->child; r != nullptr; r = r->next) {
        StopRecord rec;
        if (!decodeStopRecord(r, rec) || rec.type != kind || strcmp(rec.id, id) != 0) return false;
    }
    memcpy(out.id, id, sizeof(id));
    out.type = kind;
    out.n = 0;
    for (const cJSON* r = records->child; r != nullptr && out.n < kMaxRelayRecords; r = r->next)
        decodeStopRecord(r, out.records[out.n++]);
    return true;
}

uint8_t stepPct(int pct, int dir, int lo) {
    const int next = dir > 0 ? (pct / 10 + 1) * 10 : ((pct + 9) / 10 - 1) * 10;   // an app value off the grid snaps to it
    return static_cast<uint8_t>(next < lo ? lo : next > 100 ? 100 : next);
}

void quietLine(uint16_t start, uint16_t end, char* out, size_t cap) {
    if (start == end) { snprintf(out, cap, "Quiet hours off"); return; }
    char t[2][12];
    for (int k = 0; k < 2; ++k) {
        const int m = (k ? end : start) % 1440, h = m / 60 % 12 ? m / 60 % 12 : 12;
        const char* ap = m < 720 ? "AM" : "PM";
        if (m % 60) snprintf(t[k], sizeof(t[k]), "%d:%02d %s", h, m % 60, ap);
        else snprintf(t[k], sizeof(t[k]), "%d %s", h, ap);
    }
    // "to", not an en dash: the fonts have no U+2013
    snprintf(out, cap, "Quiet %s to %s \xC2\xB7 set in the app", t[0], t[1]);
}

}  // namespace cta
