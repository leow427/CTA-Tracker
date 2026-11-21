#pragma once
// The dock's 3 LED rows as colors (spec 6.4, 3, 7.4): one chain of 21, row r position i at index 7r + i, row 1 left
// to right from the front; position 6 (the right end, LED 7) is the picked stop, position 0 (LED 1) the farthest.
#include <stdint.h>

namespace cta {

constexpr int kLedRows = 3, kLedsPerRow = 7, kLedCount = kLedRows * kLedsPerRow;
constexpr int ledIndex(int row, int pos) { return row * kLedsPerRow + pos; }
// One pulse of LED 7 while a vehicle is at the stop, and its low point: fully off and back once a second, so it reads
// from across a room (Leo 2026-11-21: the old 45-100% over 1.9 s was too subtle).
constexpr uint32_t kLedBreathMs = 1000;
constexpr float kLedPulseLow = 0.0f;

// The look (spec 6.4), each tuned by eye on the board: a dim white track, vehicles in their line's LED color, and
// the picked stop in a hue no line or bus uses.
constexpr uint32_t kLedTrackColor = 0xFFFFFF;
constexpr float kLedTrackLevel = 0.20f;    // the track's share of full, so vehicles stand out
constexpr uint32_t kLedStopColor = 0x00FFFF;

struct LedRow {
    uint8_t mask = 0;     // bit k: LED k + 1 has a vehicle heading to its stop (litMaskTrains / litMaskBuses)
    uint32_t rgb = 0;     // 0xRRGGBB: the vehicles' LED color (ledLineColor, kLedBusColor); 0 = no pick, row dark
    uint32_t ts = 0;      // Unix time of the positions the mask came from; 0 = never
    bool due = false;     // a vehicle is at the picked stop (atStop)
    uint8_t level = 100;  // the app's LED brightness, percent; 0 is the app's Off
};

// Fills out (0xRRGGBB per LED, in chain order). A row with a pick shows the track on LEDs 1-6, kLedStopColor on
// LED 7, and its vehicles (the mask) in rgb while its positions are at most kLedStaleSec old. While due and fresh,
// LED 7 pulses in rgb instead (kLedPulseLow to 100%, a sine of period kLedBreathMs, its phase taken from ms modulo the period so
// it stays smooth at any uptime). Stale or never-fetched positions leave just the track and a steady stop (so a
// bridge whose dock went quiet doesn't pulse forever). Everything is dark at night. Every channel is scaled by
// maxBrightness / 255 (LED_MAX_BRIGHTNESS, spec 3) and the row's level, rounded.
void ledFrame(const LedRow rows[kLedRows], uint32_t now, bool night, uint8_t maxBrightness, uint32_t ms,
              uint32_t out[kLedCount]);

// The LED state as one line of space-separated decimals, for the dock's GET /leds (the LED bridge board reads it):
// "now night" then "mask rgb ts due level" for each row,
// e.g. "1763500000 0 96 16711680 1763499990 1 100 0 0 0 0 100 0 0 0 0 100".
// Writes at most n bytes (with the NUL) and returns the length, as snprintf does.
int formatLedState(char* out, int n, const LedRow rows[kLedRows], uint32_t now, bool night);
// Reads formatLedState's line. False on short, garbage or out-of-range input, leaving the outputs untouched.
bool parseLedState(const char* s, LedRow rows[kLedRows], uint32_t* now, bool* night);

}  // namespace cta
