#pragma once
// The dock's map (mockup docs/ideas/2026-11-11-dock-ui-mockup.html): a frame that fits the picked stops' approach
// zones on screen, and where a row's vehicles sit so the map and the LEDs agree.
#include <stddef.h>
#include <stdint.h>

#include "approach.h"

namespace cta {

struct MapFrame {
    float lat0, lon0;   // the points' center
    float kmX, kmY;     // km per degree
    float pxPerKm;
    float cx, cy;       // where (lat0, lon0) lands
};

// One scale for both axes, so every point fits a box (screen width w less padX each side; from top + padTop to
// h - padBottom), centered on it. A span under 0.5 km counts as 0.5 km. n == 0 frames the Loop.
MapFrame mapFrame(const LatLon* pts, size_t n, int w, int top, int h, int padX, int padTop, int padBottom);
// Screen pixel for a point, plus a pan, rounded.
void mapProject(const MapFrame& f, float lat, float lon, int panX, int panY, int& x, int& y);

// A vehicle lighting LED k (1–7) heads to stop k, so it sits 55% of the way to it from stop k - 1; with no stop
// k - 1 (LED 1, or a gap), it sits as far short of stop k as stop k + 1 is beyond it. A stop at 0, 0 is missing.
// False when it can't be placed.
bool vehicleAt(const LatLon zone[7], int k, LatLon& out);
// The LEDs (1–7) a row's vehicles are drawn at, nearest the stop first, at most max: the lit ones, plus LED 7 when
// an arrival is due there.
int vehiclesFor(uint8_t mask, bool due, int* out, int max);

}  // namespace cta
