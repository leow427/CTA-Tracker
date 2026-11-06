#pragma once
// Panel, touch and LVGL bring-up for the 4.3" board, in portrait (480x800).
#include <stdint.h>

namespace display {

constexpr int kWidth = 480;
constexpr int kHeight = 800;

bool begin();        // false if the panel or touch didn't come up (logged)
void tick();         // call from the loop: runs LVGL
int touchCount();    // fingers down right now (for the two-finger debug gesture)
void backlight(bool on);

}  // namespace display
