#include "alerts_weather.h"

#include <ArduinoJson.h>
#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

int parseAlertRoutes(const char* json, size_t len, char routes[][8], size_t max) {
    // Keep only each alert's ImpactedService (small) and drop its HTML descriptions (large). ImpactedService is kept
    // whole because CTA mixes a Service list in one alert with a lone Service object in the next.
    JsonDocument asList, asObject;
    asList["CTAAlerts"]["Alert"][0]["ImpactedService"] = true;
    asObject["CTAAlerts"]["Alert"]["ImpactedService"] = true;
    JsonDocument doc;
    if (!json::parseShapes(json, len, doc, {&asList, &asObject}, [](JsonDocument& d) {
            JsonVariantConst a = d["CTAAlerts"]["Alert"];
            return a.is<JsonArrayConst>() || a.is<JsonObjectConst>();  // none: the small unfiltered pass decides
        }))
        return -1;
    JsonVariantConst body = doc["CTAAlerts"];
    if (!body.is<JsonObjectConst>()) return -1;

    size_t n = 0;
    json::forEachItem(body["Alert"], [&](JsonObjectConst alert) {
        json::forEachItem(alert["ImpactedService"]["Service"], [&](JsonObjectConst s) {
            const char* type = s["ServiceType"].as<const char*>();
            const char* id = s["ServiceId"].as<const char*>();
            if (type == nullptr || id == nullptr || (strcmp(type, "R") != 0 && strcmp(type, "B") != 0)) return;
            char cut[8];
            copyTruncated(cut, id, sizeof(cut) - 1);
            for (size_t i = 0; i < n; ++i) {
                if (strcmp(routes[i], cut) == 0) return;
            }
            if (n >= max) return;
            memcpy(routes[n++], cut, sizeof(cut));
        });
    });
    return static_cast<int>(n);
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
