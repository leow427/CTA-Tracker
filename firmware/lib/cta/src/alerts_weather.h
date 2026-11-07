#pragma once
// Customer Alerts routes, Open-Meteo current weather, and the pet's weather accessory (spec 5, 6.3).
#include <stddef.h>

#include "cta_types.h"

namespace cta {

// Writes the distinct ServiceIds whose ServiceType is "R" (train route) or "B" (bus route), in order, up to max,
// each cut to 7 bytes. Alert and Service may each be a list or one object. A missing Alert (CTA's "no alerts"
// reply, whatever its ErrorCode) is 0 routes, as in the app. Returns the count, or −1 on malformed JSON.
int parseAlertRoutes(const char* json, size_t len, char routes[][8], size_t max);

// Reads current.weather_code (an integer) and current.temperature_2m (a number). False, leaving both untouched,
// if either is missing or the JSON is malformed.
bool parseWeather(const char* json, size_t len, int& wmo, float& tempC);

// Umbrella for WMO codes 51–67, 80–82, 95–99; otherwise Scarf for 71–77, 85–86, or tempC < 0; otherwise None.
Accessory accessoryFor(int wmo, float tempC);

}  // namespace cta
