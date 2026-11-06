#include "urls.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace cta {

static const char* kTrainBase = "https://lapi.transitchicago.com/api/1.0/";
static const char* kBusBase = "https://www.ctabustracker.com/bustime/api/v2/";

static bool put(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n >= 0 && (size_t)n < cap;
}

// "a,b,c" into buf.
static bool joined(char* buf, size_t cap, const char* const* items, size_t n) {
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
        int w = snprintf(buf + at, cap - at, i ? ",%s" : "%s", items[i]);
        if (w < 0 || (size_t)w >= cap - at) return false;
        at += w;
    }
    return true;
}

// Spaces only: the only character CTA's direction names and route lists need escaped.
static bool spaceEscaped(char* buf, size_t cap, const char* s) {
    size_t at = 0;
    for (; *s; s++) {
        const char* piece = *s == ' ' ? "%20" : nullptr;
        size_t w = piece ? 3 : 1;
        if (at + w >= cap) return false;
        if (piece) memcpy(buf + at, piece, 3);
        else buf[at] = *s;
        at += w;
    }
    buf[at] = '\0';
    return true;
}

bool trainArrivalsUrl(char* buf, size_t cap, const char* key, const char* id) {
    const char* param = id[0] == '4' ? "mapid" : "stpid";
    return put(buf, cap, "%sttarrivals.aspx?key=%s&%s=%s&max=20&outputType=JSON", kTrainBase, key, param, id);
}

bool trainPositionsUrl(char* buf, size_t cap, const char* key, const char* const* routes, size_t n) {
    char list[64];
    if (n == 0 || n > 8 || !joined(list, sizeof list, routes, n)) return false;
    return put(buf, cap, "%sttpositions.aspx?key=%s&rt=%s&outputType=JSON", kTrainBase, key, list);
}

bool busPredictionsUrl(char* buf, size_t cap, const char* key, const char* const* stpids, size_t n) {
    char list[96];
    if (n == 0 || n > 10 || !joined(list, sizeof list, stpids, n)) return false;
    return put(buf, cap, "%sgetpredictions?key=%s&stpid=%s&format=json", kBusBase, key, list);
}

bool busVehiclesUrl(char* buf, size_t cap, const char* key, const char* const* routes, size_t n) {
    char list[96];
    if (n == 0 || n > 10 || !joined(list, sizeof list, routes, n)) return false;
    return put(buf, cap, "%sgetvehicles?key=%s&rt=%s&format=json", kBusBase, key, list);
}

bool busPatternsUrl(char* buf, size_t cap, const char* key, const char* route) {
    return put(buf, cap, "%sgetpatterns?key=%s&rt=%s&format=json", kBusBase, key, route);
}

bool busRoutesUrl(char* buf, size_t cap, const char* key) {
    return put(buf, cap, "%sgetroutes?key=%s&format=json", kBusBase, key);
}

bool busDirectionsUrl(char* buf, size_t cap, const char* key, const char* route) {
    return put(buf, cap, "%sgetdirections?key=%s&rt=%s&format=json", kBusBase, key, route);
}

bool busStopsUrl(char* buf, size_t cap, const char* key, const char* route, const char* dir) {
    char d[64];
    if (!spaceEscaped(d, sizeof d, dir)) return false;
    return put(buf, cap, "%sgetstops?key=%s&rt=%s&dir=%s&format=json", kBusBase, key, route, d);
}

bool alertsUrl(char* buf, size_t cap, const char* const* routes, size_t n) {
    // The route list goes straight into buf: a handheld downtown can name 25+ routes.
    if (n == 0 || !put(buf, cap, "https://www.transitchicago.com/api/1.0/alerts.aspx?outputType=JSON&activeonly=true&routeid="))
        return false;
    size_t at = strlen(buf);
    return joined(buf + at, cap - at, routes, n);
}

bool weatherUrl(char* buf, size_t cap, double lat, double lon) {
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
    return put(buf, cap, "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
               lat, lon);
}

bool geoIpUrl(char* buf, size_t cap) { return put(buf, cap, "%s", "https://ipapi.co/json/"); }

}  // namespace cta
