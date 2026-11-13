#include "bands.h"

#include <string.h>

#include "dock_ui.h"

namespace dock {

namespace {
constexpr int PW = H;   // the panel's width
}

ui::Canvas bandCanvas(uint16_t* buf, int y0, int rows, bool cw) {
    // Buffer row r is panel row r (r = 479 - x when cw, x otherwise), its pixels running along the band's panel
    // columns.
    if (cw) return ui::Canvas::strided(buf, (W - 1) * rows - y0, -rows, 1, 0, y0, W, y0 + rows);
    return ui::Canvas::strided(buf, y0 + rows - 1, rows, -1, 0, y0, W, y0 + rows);
}

void copyBand(const uint16_t* buf, uint16_t* panel, int y0, int rows, bool cw) {
    const int col = cw ? y0 : PW - y0 - rows;
    for (int r = 0; r < W; ++r) memcpy(panel + r * PW + col, buf + r * rows, static_cast<size_t>(rows) * 2);
}

}  // namespace dock
