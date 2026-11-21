#pragma once
// CTA line colors (spec 6.5): the one table, mirrored by the app's LineColors.
#include <stdint.h>

namespace cta {

constexpr uint32_t kBusColor = 0x565A5C;

// 0xRRGGBB for a GTFS train route ID (Red, Blue, Brn, G, Org, Pink, P, Y), matched case-insensitively because
// ttpositions uses lowercase. Anything else, including null or empty, is a bus route and gets kBusColor.
uint32_t lineColor(const char* route);

// The route board's colors (spec 6.4): saturated for WS2812s, not the screen's hex values (those wash out, red turns
// pink). Tuned by eye on the board. The bus purple is the LEDs' own, unlike the screen's grey.
constexpr uint32_t kLedBusColor = 0x5000FF;   // deep violet: A000FF read close to white (Leo 2026-11-21)
// lineColor's lookup over the LED table; anything else is a bus and gets kLedBusColor.
uint32_t ledLineColor(const char* route);

}  // namespace cta
