#pragma once
// The dock performance analysis logger (docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md,
// section 2). In PERF_LOG builds only (env:perf): every hook below is wrapped in #ifdef PERF_LOG at its call site, so
// the demo build (env:dock) is unchanged.
//
// Once a second it prints a `P,` line of raw facts; events go on `E,` lines. Lines go through a ring in PSRAM and out
// over serial a whole line at a time from the loop, so no line is split by another task's print.
#ifdef PERF_LOG
#include <stdint.h>

namespace perf {

struct Inputs {
    uint8_t phase = 0;    // stress test phase, 0 when none is running
    int sections = 0;     // picked sections with arrivals
    int picked = 0;       // picked sections
    int dataAgeSec = -1;  // the oldest picked section's arrivals, -1 with none
    uint8_t screen = 0;   // dock::Screen
};

void begin();                    // after Serial.begin(); prints the column header
void header();                   // the column header again (at the start of a stress run)
bool due(uint32_t ms);           // a sample is due this loop
void sample(uint32_t ms, const Inputs& in);
void flush();                    // sends what fits; call every loop
void event(const char* fmt, ...) __attribute__((format(printf, 1, 2)));   // fmt is the part after "E,<ms>,"

// The frame loop (core 1).
void touch();                                // one touch event handled
void tapUp(uint32_t touchMs);                // a tap (or any release) at touchMs: tap-to-screen ends at the next frame
void move(uint32_t touchMs);                 // a drag sample at touchMs: drag-to-screen ends at the next frame
// A frame was shown: its draw, copy and wait times, bands copied, whether a drag was moving it, when it was done, and
// bands drawn (the last column, `drawn`: the rest the hidden buffer already held).
void frame(uint32_t drawUs, uint32_t copyUs, uint32_t waitUs, int bandsCopied, bool moving, uint32_t doneMs, int bandsDrawn);
void listLoaded(uint32_t ms);                // a picker list asked for at the loop came in after ms
void scan(uint32_t late);                    // late panel frames (display::scanStats)

// The network task (core 0).
void reqStart(char kind);                    // t trains, b buses, a alerts, w weather, g geolocation, c clock
void reqEnd(char kind, int code, uint32_t bytes, uint32_t ms);
// The network burst (stress phases 5 and 6): true when the network task should poll the next thing now.
void setBurst(bool on);
bool burstDue();

}  // namespace perf
#endif
