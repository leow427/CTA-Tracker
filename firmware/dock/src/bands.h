#pragma once
// The portrait screen (480 × 800) drawn band by band straight in the landscape panel's order (800 × 480): the panel
// is mounted a quarter turn off. cw: the panel's top edge faces right, so portrait (x, y) is panel (y, 479 - x);
// otherwise it faces left and portrait (x, y) is panel (799 - y, x). A band of portrait rows [y0, y0 + rows) is a
// band of panel columns, all 480 panel rows, so it's drawn in internal RAM (fast) and copied into the frame buffer
// (PSRAM) a row segment at a time.
#include <stdint.h>

#include "gfx2d.h"

namespace dock {

// A canvas over buf (W × rows pixels) for portrait rows [y0, y0 + rows).
ui::Canvas bandCanvas(uint16_t* buf, int y0, int rows, bool cw);
// Copies a drawn band into the panel frame buffer (800 × 480, row-major).
void copyBand(const uint16_t* buf, uint16_t* panel, int y0, int rows, bool cw);

}  // namespace dock
