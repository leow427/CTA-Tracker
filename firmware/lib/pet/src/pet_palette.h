#pragma once
// Trackie's colors from the app (ecosystem spec 5.2): config.pet's two colors repaint the body.
#include <stdint.h>

namespace pet {

// Sets palette slots 2 (body) and 3 (body light) from 0xRRGGBB colors; every other slot is left alone.
void applyColors(uint16_t palette565[], const uint32_t colors[2]);

}  // namespace pet
