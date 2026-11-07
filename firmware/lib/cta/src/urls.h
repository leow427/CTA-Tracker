#pragma once
// URLs for every service the devices call (spec 5). Mirrored by the app's CTAClient. They carry API keys: never
// log them. Each returns false (buf holds "" if cap > 0) when the URL doesn't fit in cap or an argument is invalid.
// Query values are percent-encoded; list separators are bare commas.
#include <stddef.h>

namespace cta {

// mapid= for a station ID (4xxxx), stpid= for a platform ID (3xxxx); any other id is invalid.
bool trainArrivalsUrl(char* buf, size_t cap, const char* key, const char* id);
bool trainPositionsUrl(char* buf, size_t cap, const char* key, const char* route);
// 1–10 stops per call.
bool busPredictionsUrl(char* buf, size_t cap, const char* key, const char* const* stpids, size_t n);
// 1–10 routes per call.
bool busVehiclesUrl(char* buf, size_t cap, const char* key, const char* const* routes, size_t n);
bool busPatternsUrl(char* buf, size_t cap, const char* key, const char* route);
// At least one route.
bool alertsUrl(char* buf, size_t cap, const char* const* routes, size_t n);
// latitude and longitude to 4 decimals (about 11 m); false unless finite and within ±90 / ±180.
bool weatherUrl(char* buf, size_t cap, double lat, double lon);

}  // namespace cta
