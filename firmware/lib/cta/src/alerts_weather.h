#pragma once
// Customer Alerts routes, Open-Meteo current weather, and the pet's weather accessory (spec 5, 6.3).
#include <stddef.h>

#include "cta_types.h"

namespace cta {

// Marks which of the caller's routes have an alert: alerted[i] is true when some alert lists wanted[i] as a
// ServiceType "R" (train route) or "B" (bus route) service, matched exactly after cutting the ServiceId to 7 bytes,
// as record routes are. Alert and Service may each be a list or one object; an alert can list any number of
// routes. A nullptr entry in wanted is never alerted. A service missing its type or id is skipped. A missing
// Alert (CTA's "no alerts" reply, whatever its ErrorCode) marks nothing, as in the app.
// Returns how many wanted routes are alerted, or −1 on malformed JSON, leaving alerted untouched.
int parseAlertRoutes(const char* json, size_t len, const char* const* wanted, size_t nWanted, bool* alerted);

// Reads current.weather_code (an integer) and current.temperature_2m (a number). False, leaving both untouched,
// if either is missing or the JSON is malformed.
bool parseWeather(const char* json, size_t len, int& wmo, float& tempC);

// Umbrella for WMO codes 51–67, 80–82, 95–99; otherwise Scarf for 71–77, 85–86, or tempC < 0; otherwise None.
Accessory accessoryFor(int wmo, float tempC);

}  // namespace cta
