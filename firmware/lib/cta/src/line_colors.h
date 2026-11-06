#pragma once
// CTA line colors (spec 6.5): the one table, mirrored by the app's LineColors.
#include <stdint.h>

namespace cta {

constexpr uint32_t kBusColor = 0x565A5C;

// 0xRRGGBB for a GTFS train route ID (Red, Blue, Brn, G, Org, Pink, P, Y), matched case-insensitively because
// ttpositions uses lowercase. Anything else, including null or empty, is a bus route and gets kBusColor.
uint32_t lineColor(const char* route);

}  // namespace cta
