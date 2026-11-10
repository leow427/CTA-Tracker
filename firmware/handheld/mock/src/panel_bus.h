#pragma once
// The panel's pixel sends, by DMA with the sending task asleep until they're done (main.cpp's sender, core 0).
//
// Arduino_GFX's QSPI bus sends pixels in 2 KB polled transactions: the CPU spins on each one, so core 0 was busy for
// as long as the panel took to take a frame (about 45% of core 0 while the list scrolls), and the network task, which
// runs below the sender on that core, waited. This sends the same bytes, in the same commands and the same order,
// through a device of its own on the same SPI bus: up to 16 KB per transaction, queued, with the chip select held low
// across them as the library does, and the task blocked on the driver's interrupt instead of spinning.
//
// The library's bus has to be in shared mode (Arduino_ESP32QSPI's is_shared_interface), so it doesn't keep the bus to
// itself; the library still sets the panel up and sets the brightness. Only one task may use the bus at a time:
// after set-up, start(), finish() and the library's calls all run on the sender, which calls the library only after
// finish(), so a library call never meets a send in flight. gfx->begin() initialises the bus, and the SPI driver
// puts its interrupt on the core that does, so main.cpp runs it, and begin() after it, from a task on core 0, the
// sender's core.
//
// tools/panel_bus_test.cpp checks the wire against the library's on a laptop.
#include <stdint.h>

namespace panel {

// Adds the device (cs: the display's chip-select pin, which the library drives as a plain GPIO; hz as the library's
// device; colOffset/rowOffset: the panel window's offsets, as given to Arduino_CO5300). False if the driver refused,
// and the caller keeps sending through the library.
bool begin(int cs, int32_t hz, int colOffset, int rowOffset);

// Starts sending a w × h panel window at (x, y): big-endian RGB565, row-major, w * h * 2 bytes of DMA-capable memory
// at a 4-byte boundary, a multiple of 4 bytes long, left untouched until finish(). On the wire it's exactly what
// Arduino_CO5300::draw16bitBeRGBBitmap sends (column and page address when the window changed, memory write, the
// pixels in quad mode). Finishes any send still in flight first. Returns once the pixels are queued, or, for a send
// of 2 KB or less (one of the library's own transactions), once they're sent, polled as before.
void start(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* px);
// Waits until the last start()'s pixels are all on the panel and releases the chip select. Cheap with none in flight.
void finish();

}  // namespace panel
