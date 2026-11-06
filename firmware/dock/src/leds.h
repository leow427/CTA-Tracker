#pragma once
// The route board: 3 approach rows of 7 NeoPixels (spec 6.4).
#include <stdint.h>

namespace leds {
void begin();
// Call ~30 times a second from the UI loop: draws the rows from gState.
void tick(uint32_t nowUnix, bool night);
}  // namespace leds
