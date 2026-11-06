#pragma once
// The portrait screen (spec 7.1): status strip with the pet, 3 stop sections, and full-screen states.
#include <stdint.h>

namespace ui {
void begin();
void tick();   // ~30 Hz from the loop: redraws what changed
}  // namespace ui
