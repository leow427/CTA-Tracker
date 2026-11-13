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
//
// A recorded band is drawn in two halves of its panel rows (240 each, a portrait half screen each: 15 KB, half the
// band buffer), so a half can be copied into the frame buffer while the other is drawn (dock performance analysis,
// round 2). Each half's row segments are whole 64-byte cache lines of the frame buffer, as the whole band's were, so
// the copy costs what it did. A Copier takes each half as soon as it's drawn and gives the buffer back before it's
// drawn into again: on the device a task on core 0 copies while core 1 draws (display.cpp); without one, each half
// is copied at once.
#include <stdint.h>

#include "gfx2d.h"

namespace dock {

constexpr int kBandRows = 32;   // 25 bands a frame; the band buffer is 30 KB of internal RAM
constexpr int kPanelRows = 480, kPanelCols = 800;
constexpr int kHalfRows = kPanelRows / 2;              // a half band's panel rows
constexpr int kHalfPx = kHalfRows * kBandRows;         // a half band's pixels: the band buffer holds two

// A canvas over buf for portrait rows [y0, y0 + rows) and panel rows [p0, p1) (the portrait columns they hold): buf
// holds (p1 - p0) × rows pixels, buffer row r being panel row p0 + r. The whole band is panel rows [0, 480).
ui::Canvas bandCanvas(uint16_t* buf, int y0, int rows, bool cw, int p0 = 0, int p1 = kPanelRows);
// Copies such a drawn band (or part of one) into the panel frame buffer (800 × 480, row-major).
void copyBand(const uint16_t* buf, uint16_t* panel, int y0, int rows, bool cw, int p0 = 0, int p1 = kPanelRows);
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

// A copy job: rows row segments of len pixels, src's one after another, each to dst + r * kPanelCols.
struct CopyJob {
    const uint16_t* src;
    uint16_t* dst;
    int rows, len;
};
void runCopy(const CopyJob& j, int r0, int r1);   // rows [r0, r1) of it

// Copies the drawn halves into the frame buffer behind drawFrame's back. Half h is always in buffer slot h (the band
// buffer's first or second 15 KB). put hands over a slot's job once it's drawn; take returns once every row of the
// slot's last job is in the frame buffer (copying what's left itself), after which the slot may be drawn into. Every
// put is followed by a take of the same slot before drawFrame returns.
struct Copier {
    void (*put)(void* ctx, int slot, const CopyJob& job);
    void (*take)(void* ctx, int slot);
    void* ctx;
};

struct FrameStats {
    uint32_t drawUs = 0;   // recording the frame and drawing its bands
    uint32_t copyUs = 0;   // the drawing core's share of copying: copying itself, or waiting to take a slot back
    int drawn = 0, copied = 0;
};

// Draws a frame into fb, buffer `which` of the two, through band (W × kBandRows pixels). With rec, the frame is
// recorded once and only the bands whose drawing isn't what fb holds are played (in halves) and copied, by copier if
// there is one; without rec, or when the frame can't be recorded, draw is called for every band (whole) and a band is
// copied (here) only when its pixels changed. us times it. On return, every copy into fb is done.
void drawFrame(void (*draw)(ui::Canvas& cv, void* ctx), void* ctx, ui::Recording* rec, BandMemo& memo, int which,
               uint16_t* band, uint16_t* fb, bool cw, uint32_t (*us)(), FrameStats& st, const Copier* copier = nullptr);

}  // namespace dock
