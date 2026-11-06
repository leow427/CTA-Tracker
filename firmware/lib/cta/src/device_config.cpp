#include "device_config.h"

#include "json_util.h"
#include <string.h>

#include "text.h"

namespace cta {

DeviceConfig defaultConfig() {
    DeviceConfig c{};
    strcpy(c.petName, "Trackie");                 // the app's default pet name (iOS redesign spec)
    c.petColors[0] = 0x2D6BE6;                    // Trackie's blue paint
    c.petColors[1] = 0x7DB4FF;
    c.screen = 100;
    c.led = 60;
    c.nightStart = kNightStartMin;
    c.nightEnd = kNightEndMin;
    return c;
}

static bool percent(JsonVariantConst v, uint8_t& out) {
    if (!v.is<int>()) return false;
    int n = v.as<int>();
    if (n < 0 || n > 100) return false;
    out = (uint8_t)n;
    return true;
}

static bool minuteOfDay(JsonVariantConst v, int16_t& out) {
    if (!v.is<int>()) return false;
    int n = v.as<int>();
    if (n < 0 || n > 1439) return false;
    out = (int16_t)n;
    return true;
}

bool applyConfigJson(const char* json, size_t len, DeviceConfig& cfg) {
    Doc doc;
    if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) return false;
    DeviceConfig next = cfg;

    JsonVariantConst stops = doc["stops"];
    if (!stops.isNull()) {
        if (!stops.is<JsonArrayConst>() || stops.size() > 3) return false;
        for (size_t i = 0; i < 3; i++) {
            PickedStop& p = next.stops[i];
            p = PickedStop{};
            JsonVariantConst s = stops[i];
            const char* id = s["id"] | "";
            if (!*id) continue;
            const char* type = s["type"] | "";
            if (strcmp(type, "train") == 0) p.type = Kind::Train;
            else if (strcmp(type, "bus") == 0) p.type = Kind::Bus;
            else return false;
            const char* route = s["route"] | "";
            if (!*route || strlen(id) > kIdLen || strlen(route) > kIdLen) return false;
            copyTruncated(p.id, id, kIdLen);
            copyTruncated(p.route, route, kIdLen);
            copyTruncated(p.dir, s["dir"] | "", kNameLen);
            copyTruncated(p.name, s["name"] | "", kNameLen);
        }
    }

    JsonVariantConst pet = doc["pet"];
    if (!pet.isNull()) {
        if (!pet.is<JsonObjectConst>()) return false;
        if (!pet["name"].isNull()) {
            const char* name = pet["name"];
            if (!name) return false;
            copyTruncated(next.petName, name, kPetNameLen);
        }
        JsonVariantConst colors = pet["colors"];
        if (!colors.isNull()) {
            if (!colors.is<JsonArrayConst>() || colors.size() != 2) return false;
            for (size_t i = 0; i < 2; i++) {
                if (!colors[i].is<uint32_t>() || colors[i].as<uint32_t>() > 0xFFFFFF) return false;
                next.petColors[i] = colors[i];
            }
        }
    }

    JsonVariantConst set = doc["settings"];
    if (!set.isNull()) {
        if (!set.is<JsonObjectConst>()) return false;
        if (!set["screen"].isNull() && !percent(set["screen"], next.screen)) return false;
        if (!set["led"].isNull() && !percent(set["led"], next.led)) return false;
        if (!set["night_start"].isNull() && !minuteOfDay(set["night_start"], next.nightStart)) return false;
        if (!set["night_end"].isNull() && !minuteOfDay(set["night_end"], next.nightEnd)) return false;
    }
    cfg = next;
    return true;
}

static size_t finish(JsonDocument& doc, char* buf, size_t cap) {
    if (measureJson(doc) + 1 > cap) return 0;
    return serializeJson(doc, buf, cap);
}

size_t encodeConfigJson(const DeviceConfig& cfg, bool includeStops, char* buf, size_t cap) {
    Doc doc;
    if (includeStops) {
        JsonArray stops = doc["stops"].to<JsonArray>();
        for (const PickedStop& p : cfg.stops) {
            // An empty slot keeps every key (id ""), since the app's ConfigStop decodes all five.
            JsonObject s = stops.add<JsonObject>();
            s["type"] = p.type == Kind::Bus ? "bus" : "train";
            s["route"] = p.route;
            s["dir"] = p.dir;
            s["id"] = p.id;
            s["name"] = p.name;
        }
    }
    doc["pet"]["name"] = cfg.petName;
    JsonArray colors = doc["pet"]["colors"].to<JsonArray>();
    colors.add(cfg.petColors[0]);
    colors.add(cfg.petColors[1]);
    JsonObject set = doc["settings"].to<JsonObject>();
    set["screen"] = cfg.screen;
    set["led"] = cfg.led;
    set["night_start"] = cfg.nightStart;
    set["night_end"] = cfg.nightEnd;
    return finish(doc, buf, cap);
}

size_t encodeInfoJson(const char* type, const char* fw, int battery, bool charging, bool wifiUp, const char* ssid,
                      uint32_t lastUpdate, char* buf, size_t cap) {
    Doc doc;
    doc["type"] = type;
    doc["fw"] = fw;
    if (battery >= 0) doc["battery"] = battery;
    else doc["battery"] = nullptr;
    doc["charging"] = charging;
    doc["wifi"]["connected"] = wifiUp;
    doc["wifi"]["ssid"] = ssid ? ssid : "";
    doc["last_update"] = lastUpdate;
    return finish(doc, buf, cap);
}

bool decodeWifiJson(const char* json, size_t len, uint8_t& slot, char* ssid, size_t ssidCap, char* pass,
                    size_t passCap) {
    Doc doc;
    if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) return false;
    if (!doc["slot"].is<int>()) return false;
    int s = doc["slot"];
    const char* id = doc["ssid"];
    const char* pw = doc["pass"] | "";
    if (s < 0 || s > 2 || !id) return false;
    size_t idLen = strlen(id), pwLen = strlen(pw);
    if (idLen == 0 || idLen > kSsidMax || idLen >= ssidCap || pwLen > kPassMax || pwLen >= passCap) return false;
    slot = (uint8_t)s;
    memcpy(ssid, id, idLen + 1);
    memcpy(pass, pw, pwLen + 1);
    return true;
}

}  // namespace cta
