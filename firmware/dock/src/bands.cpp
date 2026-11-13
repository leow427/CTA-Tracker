#include "bands.h"

#include <string.h>

#include "dock_ui.h"

namespace dock {

namespace {
constexpr int PW = H;   // the panel's width
static_assert(kPanelCols == H && kPanelRows == W, "the panel is the portrait screen a quarter turn off");
static_assert(2 * kHalfRows == kPanelRows, "a band is two halves");
}

ui::Canvas bandCanvas(uint16_t* buf, int y0, int rows, bool cw, int p0, int p1) {
    // Buffer row r is panel row p0 + r (panel row 479 - x when cw, x otherwise), its pixels running along the band's
    // panel columns.
    if (cw) return ui::Canvas::strided(buf, (W - 1 - p0) * rows - y0, -rows, 1, W - p1, y0, W - p0, y0 + rows);
    return ui::Canvas::strided(buf, y0 + rows - 1 - p0 * rows, rows, -1, p0, y0, p1, y0 + rows);
}

void copyBand(const uint16_t* buf, uint16_t* panel, int y0, int rows, bool cw, int p0, int p1) {
    const int col = cw ? y0 : PW - y0 - rows;
    for (int r = p0; r < p1; ++r) memcpy(panel + r * PW + col, buf + (r - p0) * rows, static_cast<size_t>(rows) * 2);
}

void runCopy(const CopyJob& j, int r0, int r1) {
    for (int r = r0; r < r1; ++r) memcpy(j.dst + r * kPanelCols, j.src + r * j.len, static_cast<size_t>(j.len) * 2);
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
               uint16_t* band, uint16_t* fb, bool cw, uint32_t (*us)(), FrameStats& st, const Copier* copier) {
    st = FrameStats();
    bool recorded = false;
    if (rec) {
        const uint32_t t0 = us();
        ui::Canvas rc = rec->begin(W, H, kBandRows);
        draw(rc, ctx);
        recorded = rec->end() && rec->bands() == BandMemo::kBands;
        st.drawUs += us() - t0;
    }
    if (recorded) {
        // Each band whose drawing isn't the one fb holds, in its two halves of panel rows: half h is drawn into slot h
        // of the band buffer and handed to the copier, which copies it while the other half (or the next band's
        // first) is drawn. Before a slot is drawn into again, the copier gives it back with every row copied.
        for (int b = 0; b < BandMemo::kBands; ++b) {
            if (memo.drawingKnown[which][b] && memo.drawing[which][b] == rec->hash(b)) continue;   // fb shows it
            const int y0 = b * kBandRows, col = cw ? y0 : PW - y0 - kBandRows;
            for (int h = 0; h < 2; ++h) {
                uint16_t* slot = band + h * kHalfPx;
                const int p0 = h * kHalfRows;
                const uint32_t c0 = us();
                if (copier) copier->take(copier->ctx, h);   // the last job from this slot, done
                const uint32_t d0 = us();
                ui::Canvas cv = bandCanvas(slot, y0, kBandRows, cw, p0, p0 + kHalfRows);
                rec->play(cv, b);
                const uint32_t d1 = us();
                const CopyJob job = {slot, fb + p0 * PW + col, kHalfRows, kBandRows};
                if (copier) copier->put(copier->ctx, h, job);
                else runCopy(job, 0, kHalfRows);
                st.drawUs += d1 - d0;
                st.copyUs += (d0 - c0) + (us() - d1);
            }
            // Drawn because its drawing changed, so its pixels almost surely did: copied without hashing them.
            ++st.drawn;
            ++st.copied;
            memo.drawing[which][b] = rec->hash(b);
            memo.drawingKnown[which][b] = true;
            memo.pixelsKnown[which][b] = false;
        }
        if (copier) {
            const uint32_t c0 = us();
            for (int h = 0; h < 2; ++h) copier->take(copier->ctx, h);   // every row in fb before it's shown
            st.copyUs += us() - c0;
        }
        return;
    }
    // Not recorded: every band drawn whole (the band buffer's two slots as one), and copied here when its pixels
    // changed, as before.
    for (int b = 0; b < BandMemo::kBands; ++b) {
        const int y0 = b * kBandRows;
        ui::Canvas cv = bandCanvas(band, y0, kBandRows, cw);
        const uint32_t d0 = us();
        draw(cv, ctx);
        const uint32_t c0 = us();
        st.drawUs += c0 - d0;
        ++st.drawn;
        const uint32_t h = pixelHash(band, W * kBandRows);
        if (!memo.pixelsKnown[which][b] || memo.pixels[which][b] != h) {
            copyBand(band, fb, y0, kBandRows, cw);
            memo.pixels[which][b] = h;
            memo.pixelsKnown[which][b] = true;
            ++st.copied;
        }
        memo.drawingKnown[which][b] = false;
        st.copyUs += us() - c0;
    }
}

}  // namespace dock
