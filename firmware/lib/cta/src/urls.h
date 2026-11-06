#pragma once
// CTA and weather request URLs. Each returns false if the URL doesn't fit in cap, or the input is out of range.
// IDs and routes are passed through as given; they come from CTA itself or the bundled tables.
#include <stddef.h>

namespace cta {
bool trainArrivalsUrl(char* buf, size_t cap, const char* key, const char* id);   // mapid= for 4xxxx, stpid= for 3xxxx
bool trainPositionsUrl(char* buf, size_t cap, const char* key, const char* const* routes, size_t n);
bool busPredictionsUrl(char* buf, size_t cap, const char* key, const char* const* stpids, size_t n);   // 1..10
bool busVehiclesUrl(char* buf, size_t cap, const char* key, const char* const* routes, size_t n);      // 1..10
bool busPatternsUrl(char* buf, size_t cap, const char* key, const char* route);
bool busRoutesUrl(char* buf, size_t cap, const char* key);
bool busDirectionsUrl(char* buf, size_t cap, const char* key, const char* route);
bool busStopsUrl(char* buf, size_t cap, const char* key, const char* route, const char* dir);
bool alertsUrl(char* buf, size_t cap, const char* const* routes, size_t n);
bool weatherUrl(char* buf, size_t cap, double lat, double lon);
bool geoIpUrl(char* buf, size_t cap);
}  // namespace cta
