#include "alerts_weather.h"

#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

int parseAlertRoutes(const char* json, size_t len, char (*routes)[kIdLen + 1], size_t max) {
    Doc doc;
    auto build = [](JsonDocument& f, bool many) {
        listItem(f["CTAAlerts"]["Alert"], many)["ImpactedService"] = true;   // its Service may be one object too
    };
    if (deserializeList(doc, json, len, build, [](JsonDocument& d) { return d["CTAAlerts"]["Alert"]; })) return -1;
    JsonVariantConst body = doc["CTAAlerts"];
    // No Alert means no alerts, whatever ErrorCode says: CTA answers a quiet route with ErrorCode "50"
    // (fixtures/cta/alerts_none.json), and the app's parser reads it the same way.
    if (!body.is<JsonObjectConst>()) return -1;
    size_t n = 0;
    forEachItem(body["Alert"], [&](JsonVariantConst a) {
        forEachItem(a["ImpactedService"]["Service"], [&](JsonVariantConst svc) {
            const char* type = svc["ServiceType"] | "";
            const char* id = svc["ServiceId"];
            if (!id || (strcmp(type, "R") != 0 && strcmp(type, "B") != 0)) return;
            for (size_t i = 0; i < n; i++)
                if (strcmp(routes[i], id) == 0) return;
            if (n < max) copyTruncated(routes[n++], id, kIdLen);
        });
    });
    return (int)n;
}

bool parseWeather(const char* json, size_t len, int& wmo, float& tempC) {
    Doc filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"] = true;
    Doc doc;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
    JsonVariantConst c = doc["current"];
    if (!c["weather_code"].is<int>() || !(c["temperature_2m"].is<float>())) return false;
    wmo = c["weather_code"];
    tempC = c["temperature_2m"];
    return true;
}

Accessory accessoryFor(int c, float tempC) {
    if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || (c >= 95 && c <= 99)) return Accessory::Umbrella;
    if ((c >= 71 && c <= 77) || (c >= 85 && c <= 86) || tempC < 0) return Accessory::Scarf;
    return Accessory::None;
}

bool parseGeoIp(const char* json, size_t len, double& lat, double& lon) {
    Doc filter;
    filter["error"] = true;
    filter["latitude"] = true;
    filter["longitude"] = true;
    Doc doc;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
    if (doc["error"] | false) return false;
    if (!doc["latitude"].is<double>() || !doc["longitude"].is<double>()) return false;
    double la = doc["latitude"], lo = doc["longitude"];
    if (la < -90 || la > 90 || lo < -180 || lo > 180) return false;
    lat = la;
    lon = lo;
    return true;
}

}  // namespace cta
