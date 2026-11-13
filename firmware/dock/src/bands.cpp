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

// Pixels read and written as 32-bit words (two at a time), in buffers of 16-bit pixels.
typedef uint32_t __attribute__((__may_alias__)) Word;

void copyBand(const uint16_t* buf, uint16_t* panel, int y0, int rows, bool cw) {
    const int col = cw ? y0 : PW - y0 - rows;
    // A band of 32 rows (the device's) into word-aligned buffers (both are): each panel row's 64 bytes as 16 words,
    // written out in line rather than through a library call a row (dock performance analysis, round 3). The same
    // bytes; anything else row by row with memcpy.
    if (rows == 32 && ((reinterpret_cast<uintptr_t>(buf) | reinterpret_cast<uintptr_t>(panel + col)) & 3) == 0) {
        const Word* s = reinterpret_cast<const Word*>(buf);
        Word* d = reinterpret_cast<Word*>(panel + col);
        for (int r = 0; r < W; ++r, s += 16, d += PW / 2) {
#pragma GCC unroll 4
            for (int k = 0; k < 16; k += 4) {   // four words at a time: loads ahead of their stores
                const Word a = s[k], b = s[k + 1], c = s[k + 2], e = s[k + 3];
                d[k] = a; d[k + 1] = b; d[k + 2] = c; d[k + 3] = e;
            }
        }
        return;
    }
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
