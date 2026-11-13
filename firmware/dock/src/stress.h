#pragma once
// The dock's stress test (docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md, section 3), in
// PERF_LOG builds only. Serial "S" starts it, "X" stops it. It drives the screens with synthetic touches fed into the
// loop exactly as the touch task's are (down, moves every 10 ms, up), on fixed timing, and turns the network burst on
// and off. Phase markers and "done" go on `E,` lines.
#ifdef PERF_LOG
#include <stdint.h>

#include "display.h"
#include "dock_ui.h"

namespace stress {

constexpr const char* kVersion = "dock-stress-v1";

void start();
void stop();
bool running();
uint8_t phase();
// Once a loop, before the touches: moves the script along. ready: the warm-up's goal (clock set, every picked
// section has arrivals). May set the screen directly at a phase start (as the serial "h" and "s" keys do).
void tick(dock::App& app, uint32_t ms, bool ready);
// The next synthetic touch due now, stamped now.
bool nextTouch(display::Touch& t);

}  // namespace stress
#endif
