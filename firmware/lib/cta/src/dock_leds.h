#pragma once
// The dock's 3 LED rows as colors (spec 6.4, 3, 7.4): one chain of 21, row r position i at index 7r + i, row 1 left
// to right from the front; position 6 (the right end, LED 7) is the picked stop, position 0 (LED 1) the farthest.
#include <stdint.h>

namespace cta {

constexpr int kLedRows = 3, kLedsPerRow = 7, kLedCount = kLedRows * kLedsPerRow;
constexpr int ledIndex(int row, int pos) { return row * kLedsPerRow + pos; }
// One breath of LED 7: 2π × 300 ms, the mockup's sin(ms / 300), rounded to whole ms.
constexpr uint32_t kLedBreathMs = 1885;

struct LedRow {
    uint8_t mask = 0;    // bit k: LED k + 1 has a vehicle heading to its stop (litMaskTrains / litMaskBuses)
    uint32_t rgb = 0;    // 0xRRGGBB: the line's color, or kBusColor
    uint32_t ts = 0;     // Unix time of the positions the mask came from; 0 = never
    bool due = false;    // the stop's next live arrival is within kHypedMaxMin (and that data is fresh)
};

// Fills out (0xRRGGBB per LED, in chain order). A row is dark when its positions are more than kLedStaleSec old (or
// never fetched); everything is dark at night. LED 7 breathes (45–100%, a sine of period kLedBreathMs, its phase
// taken from ms modulo the period so it stays smooth at any uptime) while due.
// Every channel is scaled by maxBrightness / 255, rounded (LED_MAX_BRIGHTNESS, spec 3).
void ledFrame(const LedRow rows[kLedRows], uint32_t now, bool night, uint8_t maxBrightness, uint32_t ms,
              uint32_t out[kLedCount]);

}  // namespace cta
