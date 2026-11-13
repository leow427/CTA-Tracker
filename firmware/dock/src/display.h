#pragma once
// The 4.3" panel (RGB, 800 × 480, two frame buffers in PSRAM), its GT911 touch, and the CH422G expander that holds
// their resets and the backlight, used in portrait 480 × 800 (DISPLAY_ROTATE_CW in pins.h).
#include <stdint.h>

#include "gfx2d.h"

namespace display {

bool begin();
// Draws a frame into the hidden frame buffer, which the panel switches to at its next vertical sync. Returns once the
// old one is free to draw into. draw is called once, on a canvas that records the frame (ui::Recording), which is
// then played band by band, only into the bands the hidden buffer doesn't already hold (bands.h); a frame that can't
// be recorded calls draw once per band of portrait rows, on a canvas clipped to it, as do the diagnostic pattern and
// single-buffer modes. draw must draw the same frame each time it's called. True when the frame went to the panel and
// render waited for the panel to take it (the loop then needn't sleep before its next pass).
bool render(void (*draw)(ui::Canvas& cv, void* ctx), void* ctx);

// Restarts the panel (deletes and recreates it with the same timing; the screen is black for a moment until the next
// frame). A refill that runs late can leave the panel's bounce buffers out of step with its DMA for good (stale
// slivers at the start of every few lines), and only a restart brings them back. With XIP from PSRAM (the high_perf
// SDK, platformio.ini) flash writes no longer stall the refill, so a restart is only asked for when a frame comes
// late (the backstop). requestRecover() asks for one at the next frame (any task); recover() runs it now (the loop).
void requestRecover();
void recover();
void setAutoRecover(bool on);   // diagnostics: off leaves a late frame alone, to compare
bool autoRecovering();
uint32_t recoveries();

// Diagnostic modes (serial keys in main.cpp), for finding the tearing by eye. Normal: as above. Freeze: the next frame
// is drawn, then nothing is drawn or copied. Pattern: the next frame (a test pattern) goes into both buffers, then
// nothing moves. Single: every frame drawn straight into the buffer on screen, never swapped.
enum class Diag : uint8_t { Normal, Freeze, Pattern, Single };
void setDiag(Diag d);
Diag diag();
// The panel timing variants (0 is the build's); setTiming recreates the panel with one.
int timingCount();
int timing();
int bootTiming();   // the timing the dock starts with (and "0" goes back to)
const char* timingName(int i);
bool setTiming(int i);

// Touch, read on its own task every 10 ms and queued in portrait coordinates with each sample's time.
struct Touch {
    enum Kind : uint8_t { Down, Move, Up } kind;
    uint8_t fingers;
    int16_t x, y;
    uint32_t ms;
};
bool nextTouch(Touch& t);
bool touchOk();
// The last frame's time drawing, copying bands into the frame buffer, and waiting for the panel's switch.
void lastTimes(uint32_t& drawUs, uint32_t& copyUs, uint32_t& waitUs);
// Bands the last frame copied into the frame buffer (the rest were unchanged).
int lastBands();
// Bands the last frame drew (the rest the hidden buffer already held, bands.h).
int lastBandsDrawn();
// Frames whose buffer switch wasn't confirmed within 80 ms (should stay 0).
uint32_t timeouts();
// Since the last call: frames the panel read, how many came off their period by more than a bounce buffer's time
// (the refill interrupt ran late), the longest gap, and the nominal period.
void scanStats(uint32_t& frames, uint32_t& late, uint32_t& maxGapUs, uint32_t& periodUs);
// The backlight on or off (the CH422G's EXIO2).
void backlight(bool on);
// Sends the frame on screen over serial, run-length coded (tools/frame_grab.py turns it into a PNG).
void dumpFrame();

}  // namespace display
