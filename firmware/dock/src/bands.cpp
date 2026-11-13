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

uint32_t pixelHash(const uint16_t* px, int n) {
    const uint32_t* w = reinterpret_cast<const uint32_t*>(px);   // the band buffer is word-aligned
    uint32_t h = 2166136261u;
    for (int i = 0; i < n / 2; ++i) h = (h ^ w[i]) * 16777619u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    return h ^ (h >> 12);
}

void BandMemo::forget() {
    memset(drawingKnown, 0, sizeof(drawingKnown));
    memset(pixelsKnown, 0, sizeof(pixelsKnown));
}

void drawFrame(void (*draw)(ui::Canvas& cv, void* ctx), void* ctx, ui::Recording* rec, BandMemo& memo, int which,
               uint16_t* band, uint16_t* fb, bool cw, uint32_t (*us)(), FrameStats& st) {
    st = FrameStats();
    bool recorded = false;
    if (rec) {
        const uint32_t t0 = us();
        ui::Canvas rc = rec->begin(W, H, kBandRows);
        draw(rc, ctx);
        recorded = rec->end() && rec->bands() == BandMemo::kBands;
        st.drawUs += us() - t0;
    }
    for (int b = 0; b < BandMemo::kBands; ++b) {
        if (recorded && memo.drawingKnown[which][b] && memo.drawing[which][b] == rec->hash(b)) continue;   // fb shows it
        const int y0 = b * kBandRows;
        ui::Canvas cv = bandCanvas(band, y0, kBandRows, cw);
        const uint32_t d0 = us();
        if (recorded) rec->play(cv, b);
        else draw(cv, ctx);
        const uint32_t c0 = us();
        st.drawUs += c0 - d0;
        ++st.drawn;
        if (recorded) {
            // Drawn because its drawing changed, so its pixels almost surely did: copied without hashing them.
            copyBand(band, fb, y0, kBandRows, cw);
            ++st.copied;
            memo.drawing[which][b] = rec->hash(b);
            memo.drawingKnown[which][b] = true;
            memo.pixelsKnown[which][b] = false;
        } else {
            const uint32_t h = pixelHash(band, W * kBandRows);
            if (!memo.pixelsKnown[which][b] || memo.pixels[which][b] != h) {
                copyBand(band, fb, y0, kBandRows, cw);
                memo.pixels[which][b] = h;
                memo.pixelsKnown[which][b] = true;
                ++st.copied;
            }
            memo.drawingKnown[which][b] = false;
        }
        st.copyUs += us() - c0;
    }
}

}  // namespace dock
