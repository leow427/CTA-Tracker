#include "urls.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace cta {

namespace {

constexpr const char* kTrainBase = "https://lapi.transitchicago.com/api/1.0/";
constexpr const char* kBusBase = "https://www.ctabustracker.com/bustime/api/v2/";
constexpr size_t kMaxPerCall = 10;

// Appends into a fixed buffer, never writing past cap; ok() is false once anything didn't fit.
class Url {
public:
    Url(char* buf, size_t cap) : buf_(buf), cap_(cap) {
        if (buf_ == nullptr || cap_ == 0) ok_ = false;
    }
    Url& raw(const char* s) {
        while (ok_ && *s) put(*s++);
        return *this;
    }
    Url& value(const char* s) {  // RFC 3986 unreserved characters pass; everything else is %XX
        static const char kHex[] = "0123456789ABCDEF";
        for (; ok_ && s != nullptr && *s; ++s) {
            const unsigned char c = static_cast<unsigned char>(*s);
            const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                               c == '_' || c == '.' || c == '~';
            if (plain) {
                put(static_cast<char>(c));
            } else {
                put('%');
                put(kHex[c >> 4]);
                put(kHex[c & 0xF]);
            }
        }
        return *this;
    }
    Url& list(const char* const* items, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            if (i) raw(",");
            value(items[i]);
        }
        return *this;
    }
    bool done() {
        if (ok_ && n_ < cap_) {
            buf_[n_] = '\0';
            return true;
        }
        if (buf_ != nullptr && cap_ > 0) buf_[0] = '\0';
        return false;
    }

private:
    void put(char c) {
        if (n_ + 1 >= cap_) {  // keep room for the NUL
            ok_ = false;
            return;
        }
        buf_[n_++] = c;
    }
    char* buf_;
    size_t cap_;
    size_t n_ = 0;
    bool ok_ = true;
};

bool fail(char* buf, size_t cap) {
    if (buf != nullptr && cap > 0) buf[0] = '\0';
    return false;
}

}  // namespace

bool trainArrivalsUrl(char* buf, size_t cap, const char* key, const char* id) {
    if (id == nullptr || (id[0] != '4' && id[0] != '3')) return fail(buf, cap);
    return Url(buf, cap)
        .raw(kTrainBase).raw("ttarrivals.aspx?key=").value(key)
        .raw(id[0] == '3' ? "&stpid=" : "&mapid=").value(id)
        .raw("&outputType=JSON")
        .done();
}

bool trainPositionsUrl(char* buf, size_t cap, const char* key, const char* route) {
    return Url(buf, cap)
        .raw(kTrainBase).raw("ttpositions.aspx?key=").value(key).raw("&rt=").value(route).raw("&outputType=JSON")
        .done();
}

bool busPredictionsUrl(char* buf, size_t cap, const char* key, const char* const* stpids, size_t n) {
    if (n == 0 || n > kMaxPerCall) return fail(buf, cap);
    return Url(buf, cap)
        .raw(kBusBase).raw("getpredictions?key=").value(key).raw("&stpid=").list(stpids, n).raw("&format=json")
        .done();
}

bool busVehiclesUrl(char* buf, size_t cap, const char* key, const char* const* routes, size_t n) {
    if (n == 0 || n > kMaxPerCall) return fail(buf, cap);
    return Url(buf, cap)
        .raw(kBusBase).raw("getvehicles?key=").value(key).raw("&rt=").list(routes, n).raw("&format=json")
        .done();
}

bool busPatternsUrl(char* buf, size_t cap, const char* key, const char* route) {
    return Url(buf, cap).raw(kBusBase).raw("getpatterns?key=").value(key).raw("&rt=").value(route).raw("&format=json").done();
}

bool alertsUrl(char* buf, size_t cap, const char* const* routes, size_t n) {
    if (n == 0) return fail(buf, cap);
    return Url(buf, cap)
        .raw("https://www.transitchicago.com/api/1.0/alerts.aspx?outputType=JSON&activeonly=true&routeid=")
        .list(routes, n)
        .done();
}

bool weatherUrl(char* buf, size_t cap, double lat, double lon) {
    if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180) return fail(buf, cap);
    char coords[48];  // "latitude=-90.0000&longitude=-180.0000" is 37
    const int len = snprintf(coords, sizeof(coords), "latitude=%.4f&longitude=%.4f", lat, lon);
    if (len < 0 || static_cast<size_t>(len) >= sizeof(coords)) return fail(buf, cap);
    return Url(buf, cap)
        .raw("https://api.open-meteo.com/v1/forecast?").raw(coords).raw("&current=temperature_2m,weather_code")
        .done();
}

}  // namespace cta
