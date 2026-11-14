#pragma once
// Band copies into the panel's PSRAM frame buffers by GDMA (memory to memory) instead of the CPU.
//
// A CPU copy into PSRAM goes through the 32 KB data cache: every 64-byte line it writes is first read in from PSRAM,
// and the frame's 768 KB stream through the cache and push out what the drawing needs. That made the copy about
// 17 ms of a moving frame and slowed the drawing too. The DMA writes PSRAM directly, without those reads, while the
// CPU draws the next band into the other band buffer (bands.h, BandCopier).
//
// The DMA bypasses the cache, so the cache must not hold a line of the frame buffer the DMA writes: a stale clean line
// would hide the new pixels from the panel's refill (which reads the frame buffer through the cache), and a dirty one
// could be written back over them. finish() drops the frame buffer's lines from the cache once every copy has landed,
// and writeBack() must follow any CPU write into a frame buffer (display.cpp: the memset at panel start, the
// single-buffer diagnostic), so no dirty line is left behind.
#include <stdint.h>

#include "bands.h"

namespace dmacopy {

// Sets up the channel pair and descriptors (6 KB of internal RAM). False: no DMA, copy with the CPU.
bool begin();
// The copier for dock::drawFrame (its start and finish below).
const dock::BandCopier* copier();
// Starts copying a drawn band (W × rows, bands.h's layout) into the panel buffer fb at portrait rows [y0, y0 + rows),
// after waiting for the copy before it. A band of any height but kBandRows is copied by the CPU at once.
void start(const uint16_t* band, uint16_t* fb, int y0, int rows, bool cw);
// Waits for the last copy, then drops fb's lines from the cache: fb is ready for the panel.
void finish(uint16_t* fb);
// Writes fb's dirty lines back to PSRAM and drops them (after the CPU writes into a frame buffer).
void writeBack(uint16_t* fb);
// Copies that didn't land in time and were redone by the CPU (should stay 0).
uint32_t fallbacks();

}  // namespace dmacopy
