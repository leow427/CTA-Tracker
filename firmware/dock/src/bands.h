#pragma once
// The portrait screen (480 × 800) drawn band by band straight in the landscape panel's order (800 × 480): the panel
// is mounted a quarter turn off. cw: the panel's top edge faces right, so portrait (x, y) is panel (y, 479 - x);
// otherwise it faces left and portrait (x, y) is panel (799 - y, x). A band of portrait rows [y0, y0 + rows) is a
// band of panel columns, all 480 panel rows, so it's drawn in internal RAM (fast) and copied into the frame buffer
// (PSRAM) a row segment at a time.
//
// A frame is recorded once (ui::Recording) and played band by band. Each frame buffer remembers, per band, the hash of
// the drawing it holds: a band whose drawing is the one the buffer already holds isn't drawn or copied at all (at rest
// that's the whole screen: Trackie's bob and the blinks alternate with the buffers, so each buffer keeps its own
// half). A frame that can't be recorded draws every band, and copies only those whose pixels' hash changed.
#include <stdint.h>

#include "gfx2d.h"

namespace dock {

constexpr int kBandRows = 32;   // 25 bands a frame; the band buffer is 30 KB of internal RAM

// A canvas over buf (W × rows pixels) for portrait rows [y0, y0 + rows).
ui::Canvas bandCanvas(uint16_t* buf, int y0, int rows, bool cw);
// Copies a drawn band into the panel frame buffer (800 × 480, row-major).
void copyBand(const uint16_t* buf, uint16_t* panel, int y0, int rows, bool cw);
// FNV-1a over 32-bit words, with a final mix: any change to a band changes it but for a 1-in-4-billion collision.
uint32_t pixelHash(const uint16_t* px, int n);

// What each of the panel's two frame buffers holds, band by band.
struct BandMemo {
    static constexpr int kBands = 800 / kBandRows;
    uint64_t drawing[2][kBands];   // the recorded drawing's hash (ui::Recording::hash)
    uint32_t pixels[2][kBands];    // pixelHash
    bool drawingKnown[2][kBands], pixelsKnown[2][kBands];
    void forget();                 // nothing known: new buffers, or drawn some other way
};

struct FrameStats {
    uint32_t drawUs = 0;   // recording the frame and drawing its bands
    uint32_t copyUs = 0;   // hashing drawn bands and copying the changed ones
    int drawn = 0, copied = 0;
};

// Copies drawn bands into the frame buffer while the CPU draws the next one (the dock's GDMA, dma_copy.h). start
// returns at once, once the copy before it has landed (so the other band buffer is free again); finish returns once
// every copy has landed and fb is ready for the panel.
struct BandCopier {
    void (*start)(const uint16_t* band, uint16_t* fb, int y0, int rows, bool cw);
    void (*finish)(uint16_t* fb);
};

// Draws a frame into fb, buffer `which` of the two, through band (W × kBandRows pixels). With rec, the frame is
// recorded once and only the bands whose drawing isn't what fb holds are played and copied; without it, or when the
// frame can't be recorded, draw is called for every band and a band is copied only when its pixels changed. us times
// it. With copier and a second band buffer band2, bands are drawn into band and band2 in turn and copied by copier
// while the next one is drawn; otherwise each is copied by copyBand before the next is drawn.
void drawFrame(void (*draw)(ui::Canvas& cv, void* ctx), void* ctx, ui::Recording* rec, BandMemo& memo, int which,
               uint16_t* band, uint16_t* fb, bool cw, uint32_t (*us)(), FrameStats& st, uint16_t* band2 = nullptr,
               const BandCopier* copier = nullptr);

}  // namespace dock
