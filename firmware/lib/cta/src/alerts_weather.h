#pragma once
#include <stddef.h>

#include "cta_types.h"

namespace cta {
// Customer Alerts: distinct ServiceIds of type "R" (train route) or "B" (bus route), in order. `Alert` and
// `Service` may each be one object. A response without `Alert` (CTA's "no active alerts", ErrorCode 50) is 0.
// Returns the count, or -1 if malformed.
int parseAlertRoutes(const char* json, size_t len, char (*routes)[kIdLen + 1], size_t max);
// Open-Meteo current=temperature_2m,weather_code.
bool parseWeather(const char* json, size_t len, int& wmo, float& tempC);
// Spec 6.3: rain codes give the umbrella (even below 0 °C), then snow codes or frost give the scarf.
Accessory accessoryFor(int wmo, float tempC);
// ipapi.co/json: the dock's approximate location (spec 7.5). False on {"error": true} or junk.
bool parseGeoIp(const char* json, size_t len, double& lat, double& lon);
}  // namespace cta
