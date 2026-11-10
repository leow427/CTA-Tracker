#pragma once
// The power analysis logger (docs/superpowers/specs/2026-11-09-handheld-power-analysis-design.md, section 2). In
// POWER_LOG builds only (env:power): every call below compiles to nothing elsewhere, so the demo build is unchanged.
//
// Once a second it prints a `P,` line of raw facts (never estimated current: the laptop's model turns these into
// milliamps), and events go on `E,` lines. Every line goes through a ring in PSRAM and out over USB serial a whole
// line at a time, so nothing is lost while the link is down and no line is split by another task's print.
#include <stdint.h>

namespace plog {

#ifdef POWER_LOG

// Filled by the loop once a second; the logger adds CPU, Wi-Fi, frames, requests, and memory itself.
struct Inputs {
    uint8_t phase = 0;        // stress test phase, 0 when none is running
    bool dim = false;
    uint8_t brightness = 0;   // panel brightness, 0-255
    uint16_t usbMv = 0, sysMv = 0;
    float pmuTempC = 0;
    bool battery = false;
    int battPct = -1;
    uint16_t battMv = 0;
    int trainCards = 0, busCards = 0;
    int dataAgeSec = -1;      // the oldest card's data, -1 with none
};

void begin();                          // after Serial.begin(); prints the column header
void header();                         // prints the column header again (at the start of a stress run)
bool due(uint32_t ms);                 // a sample is due this loop
void sample(uint32_t ms, const Inputs& in);
void flush();                          // sends what fits; call every loop

// From the frame loop (core 1).
void frame(bool moving, uint32_t ms);  // a frame was drawn
bool litWanted();                      // measure this frame's lit fraction
void litStrip(const uint16_t* px, int n);   // a strip as sent to the panel (byte-swapped RGB565)
void litDone();
void tap();                            // a tap that changes the screen: tap-to-screen starts now
void frameQueued(uint16_t seq);        // frame `seq` has been queued to the sender
void touch();                          // one touch event
// From the panel sender (core 0).
void sent(uint32_t px);
void frameDone(uint16_t seq);
// From the network task (core 0).
void reqStart(char kind);              // t trains, b buses, a alerts, w weather
void reqEnd(char kind, int code, uint32_t bytes, uint32_t ms);
// Anywhere: an `E,` line. fmt is the part after "E,<ms>,".
void event(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#endif

}  // namespace plog
