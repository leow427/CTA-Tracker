#include "alerts_weather.h"

#include <ArduinoJson.h>
#include <string.h>

#include <initializer_list>
#include <vector>

#include "json_util.h"
#include "text.h"

namespace cta {

int parseAlertRoutes(const char* json, size_t len, const char* const* wanted, size_t nWanted, bool* alerted) {
    // Keep only each service's ServiceType and ServiceId, dropping the HTML descriptions and the other service
    // fields. A filter holds one shape per path, and CTA mixes a Service list in one alert with a lone Service
    // object in the next, so this reads the reply twice: once keeping Service lists, once keeping lone Services.
    // Each alert's services survive in exactly one of the two documents. Alert itself may be a list or one object.
    std::vector<char> marks(nWanted, 0);
    for (int pass = 0; pass < 2; ++pass) {
        JsonDocument asList, asObject;  // Alert as a list, Alert as one object
        const JsonObject alerts[2] = {asList["CTAAlerts"]["Alert"][0].to<JsonObject>(),
                                      asObject["CTAAlerts"]["Alert"].to<JsonObject>()};
        for (JsonObject alert : alerts) {
            JsonObject service = pass == 0 ? alert["ImpactedService"]["Service"][0].to<JsonObject>()
                                           : alert["ImpactedService"]["Service"].to<JsonObject>();
            service["ServiceType"] = true;
            service["ServiceId"] = true;
        }
        JsonDocument doc;
        if (!json::parseShapes(json, len, doc, {&asList, &asObject}, [](JsonDocument& d) {
                JsonVariantConst a = d["CTAAlerts"]["Alert"];
                return a.is<JsonArrayConst>() || a.is<JsonObjectConst>();  // none: the small unfiltered pass decides
            }))
            return -1;
        if (!doc["CTAAlerts"].is<JsonObjectConst>()) return -1;

        json::forEachItem(doc["CTAAlerts"]["Alert"], [&](JsonObjectConst alert) {
            json::forEachItem(alert["ImpactedService"]["Service"], [&](JsonObjectConst s) {
                const char* type = s["ServiceType"].as<const char*>();
                const char* id = s["ServiceId"].as<const char*>();
                if (type == nullptr || id == nullptr || (strcmp(type, "R") != 0 && strcmp(type, "B") != 0)) return;
                char cut[8];
                copyTruncated(cut, id, sizeof(cut) - 1);
                for (size_t i = 0; i < nWanted; ++i) {
                    if (wanted[i] != nullptr && strcmp(wanted[i], cut) == 0) marks[i] = 1;
                }
            });
        });
    }
    int n = 0;
    for (size_t i = 0; i < nWanted; ++i) {
        alerted[i] = marks[i] != 0;
        n += marks[i];
    }
    return n;
}

bool parseWeather(const char* json, size_t len, int& wmo, float& tempC) {
    if (json == nullptr) return false;
    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
    JsonVariantConst code = doc["current"]["weather_code"], temp = doc["current"]["temperature_2m"];
    if (!code.is<int>() || !temp.is<float>()) return false;
    wmo = code.as<int>();
    tempC = temp.as<float>();
    return true;
}

Accessory accessoryFor(int wmo, float tempC) {
    if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82) || (wmo >= 95 && wmo <= 99)) return Accessory::Umbrella;
    if ((wmo >= 71 && wmo <= 77) || (wmo >= 85 && wmo <= 86) || tempC < 0) return Accessory::Scarf;
    return Accessory::None;
}

}  // namespace cta
