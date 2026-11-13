#pragma once
// The route board's 21 NeoPixels (spec 3, 6.4), driven from a task on core 0 (away from the panel's interrupts on
// core 1). The loop posts what the rows show; the task works out cta::ledFrame itself 25 times a second, so LED 7's
// breathing doesn't stall while the loop draws a frame, and sends only what changed.
#include <stdint.h>

#include "dock_leds.h"

namespace leds {

void begin();
// The rows (mask, color, positions' time, due), the time, and night hours, as they stand now.
void post(const cta::LedRow rows[cta::kLedRows], uint32_t now, bool night);

}  // namespace leds
