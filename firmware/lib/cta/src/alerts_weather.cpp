#include "alerts_weather.h"

#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

int parseAlertRoutes(const char* json, size_t len, const char* const* wanted, size_t nWanted, bool* alerted) {
    // Alert may be a list or one object, and so may each alert's ImpactedService.Service.
    json::Doc doc(json::parse(json, len));
    const cJSON* root = json::at(doc.root, "CTAAlerts");
    if (!cJSON_IsObject(root)) return -1;
    for (size_t i = 0; i < nWanted; ++i) alerted[i] = false;
    json::forEachItem(json::at(root, "Alert"), [&](const cJSON* alert) {
        json::forEachItem(json::at(json::at(alert, "ImpactedService"), "Service"), [&](const cJSON* s) {
            const char* type = json::str(json::at(s, "ServiceType"));
            const char* id = json::str(json::at(s, "ServiceId"));
            if (type == nullptr || id == nullptr || (strcmp(type, "R") != 0 && strcmp(type, "B") != 0)) return;
            char cut[8];
            copyTruncated(cut, id, sizeof(cut) - 1);
            for (size_t i = 0; i < nWanted; ++i) {
                if (wanted[i] != nullptr && strcmp(wanted[i], cut) == 0) alerted[i] = true;
            }
        });
    });
    int n = 0;
    for (size_t i = 0; i < nWanted; ++i) n += alerted[i];
    return n;
}

bool parseWeather(const char* json, size_t len, int& wmo, float& tempC) {
    json::Doc doc(json::parse(json, len));
    const cJSON* current = json::at(doc.root, "current");
    const cJSON* code = json::at(current, "weather_code");
    const cJSON* temp = json::at(current, "temperature_2m");
    int w;
    if (!json::isInt(code, w) || !cJSON_IsNumber(temp)) return false;
    wmo = w;
    tempC = static_cast<float>(temp->valuedouble);
    return true;
}

bool parseGeoIp(const char* json, size_t len, double& lat, double& lon) {
    json::Doc doc(json::parse(json, len));
    if (doc.root == nullptr || json::asBool(json::at(doc.root, "error"))) return false;
    const cJSON* la = json::at(doc.root, "latitude");
    const cJSON* lo = json::at(doc.root, "longitude");
    if (!cJSON_IsNumber(la) || !cJSON_IsNumber(lo)) return false;
    const double a = la->valuedouble, o = lo->valuedouble;
    if (!(a >= -90 && a <= 90 && o >= -180 && o <= 180)) return false;
    lat = a;
    lon = o;
    return true;
}

Accessory accessoryFor(int wmo, float tempC) {
    if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82) || (wmo >= 95 && wmo <= 99)) return Accessory::Umbrella;
    if ((wmo >= 71 && wmo <= 77) || (wmo >= 85 && wmo <= 86) || tempC < 0) return Accessory::Scarf;
    return Accessory::None;
}

}  // namespace cta
