#include "map_frame.h"

#include <math.h>

namespace cta {

namespace {
constexpr float kKmPerDeg = 111.32f;
bool has(const LatLon& p) { return p.lat != 0 || p.lon != 0; }
}  // namespace

MapFrame mapFrame(const LatLon* pts, size_t n, int w, int top, int h, int padX, int padTop, int padBottom) {
    static const LatLon kLoop = {41.8781f, -87.6298f};
    if (n == 0) { pts = &kLoop; n = 1; }
    float la0 = pts[0].lat, la1 = la0, lo0 = pts[0].lon, lo1 = lo0;
    for (size_t i = 1; i < n; ++i) {
        la0 = fminf(la0, pts[i].lat); la1 = fmaxf(la1, pts[i].lat);
        lo0 = fminf(lo0, pts[i].lon); lo1 = fmaxf(lo1, pts[i].lon);
    }
    MapFrame f;
    f.lat0 = (la0 + la1) / 2;
    f.lon0 = (lo0 + lo1) / 2;
    f.kmY = kKmPerDeg;
    f.kmX = kKmPerDeg * cosf(f.lat0 * 3.14159265f / 180);
    const float spanX = (lo1 - lo0) * f.kmX, spanY = (la1 - la0) * f.kmY;
    const float boxW = w - 2.f * padX, boxH = static_cast<float>(h - top - padTop - padBottom);
    f.pxPerKm = fminf(boxW / fmaxf(spanX, 0.5f), boxH / fmaxf(spanY, 0.5f));
    f.cx = w / 2.f;
    f.cy = top + padTop + boxH / 2;
    return f;
}

void mapProject(const MapFrame& f, float lat, float lon, int panX, int panY, int& x, int& y) {
    x = static_cast<int>(lroundf(f.cx + (lon - f.lon0) * f.kmX * f.pxPerKm)) + panX;
    y = static_cast<int>(lroundf(f.cy - (lat - f.lat0) * f.kmY * f.pxPerKm)) + panY;
}

bool vehicleAt(const LatLon zone[7], int k, LatLon& out) {
    if (k < 1 || k > 7 || !has(zone[k - 1])) return false;
    const LatLon& b = zone[k - 1];
    LatLon a;
    if (k > 1 && has(zone[k - 2])) a = zone[k - 2];
    else if (k < 7 && has(zone[k])) a = LatLon{2 * b.lat - zone[k].lat, 2 * b.lon - zone[k].lon};
    else return false;
    constexpr float f = 0.55f;
    out = LatLon{a.lat + (b.lat - a.lat) * f, a.lon + (b.lon - a.lon) * f};
    return true;
}

int vehiclesFor(uint8_t mask, bool due, int* out, int max) {
    if (due) mask |= 1 << 6;
    int n = 0;
    for (int k = 7; k >= 1 && n < max; --k)
        if ((mask >> (k - 1)) & 1) out[n++] = k;
    return n;
}

}  // namespace cta
