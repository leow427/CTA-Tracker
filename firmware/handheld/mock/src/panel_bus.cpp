#include "panel_bus.h"

#include <Arduino.h>
#include <driver/spi_master.h>
#include <soc/gpio_reg.h>
#include <string.h>

#include "Arduino_GFX_Library.h"   // ESP32QSPI_SPI_HOST: the bus the library set up

namespace panel {

namespace {

// The CO5300's commands, as Arduino_CO5300 sends them: 0x02 then the command in the address phase, one line; the
// pixels as 0x32 with address 0x003C00 (memory write continue), the data on four lines.
constexpr uint8_t kCaset = 0x2A, kPaset = 0x2B, kRamwr = 0x2C;
constexpr size_t kChunkMax = 16384;   // bytes per transaction; the library's bus takes up to 16392
constexpr int kQueue = 4;             // transactions in flight: a whole strip (466 × 48 × 2 = 44,736 bytes) is 3
// A send that fits one of the library's own transactions (2 KB, about 50 µs on the wire) goes out polled, as before:
// that small, the interrupt and the task switches would cost about as much CPU as the wait, and add latency.
constexpr size_t kPollMax = 2048;

spi_device_handle_t dev = nullptr;
size_t chunk = 0;
uint32_t csMask = 0, csSetReg = 0, csClrReg = 0;
int xOff = 0, yOff = 0;
int16_t curX = -1, curY = -1, curW = -1, curH = -1;   // the window last set; none yet

spi_transaction_ext_t cmdT;          // in internal RAM (static): the driver reads them from its interrupt
spi_transaction_ext_t ring[kQueue];  // the pixels' transactions; ring[next] is free or the oldest in flight
int next = 0, inFlight = 0;
bool selected = false;               // chip select low for a pixel send in flight

void csLow() { REG_WRITE(csClrReg, csMask); }
void csHigh() { REG_WRITE(csSetReg, csMask); }

// One command and up to 4 data bytes, polled (a few µs), framed by the chip select: as the library's writeCommand
// (no data) and writeC8D16D16 (4 bytes).
void command(uint8_t c, const uint8_t* d, int n) {
    memset(&cmdT, 0, sizeof(cmdT));
    cmdT.base.flags = (n ? SPI_TRANS_USE_TXDATA : 0) | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    cmdT.base.cmd = 0x02;
    cmdT.base.addr = static_cast<uint32_t>(c) << 8;
    for (int i = 0; i < n; ++i) cmdT.base.tx_data[i] = d[i];
    cmdT.base.length = static_cast<size_t>(n) * 8;
    csLow();
    spi_device_polling_transmit(dev, &cmdT.base);
    csHigh();
}

void command16x2(uint8_t c, uint16_t a, uint16_t b) {
    const uint8_t d[4] = {static_cast<uint8_t>(a >> 8), static_cast<uint8_t>(a), static_cast<uint8_t>(b >> 8),
                          static_cast<uint8_t>(b)};
    command(c, d, 4);
}

void reap() {   // the oldest transaction in flight, once it's done (the task sleeps until then)
    spi_transaction_t* done = nullptr;
    spi_device_get_trans_result(dev, &done, portMAX_DELAY);
    --inFlight;
}

void drain() {
    while (inFlight > 0) reap();
}

// Pixels, as the library's writeBytes: the memory-write command on the first transaction, the rest data alone.
void pixels(spi_transaction_ext_t& t, const uint8_t* p, size_t n, bool first) {
    memset(&t, 0, sizeof(t));
    if (first) {
        t.base.flags = SPI_TRANS_MODE_QIO;
        t.base.cmd = 0x32;
        t.base.addr = 0x003C00;
    } else {
        t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    }
    t.base.tx_buffer = p;
    t.base.length = n * 8;
}

void queue(const uint8_t* p, size_t n, bool first) {
    if (inFlight == kQueue) reap();
    spi_transaction_ext_t& t = ring[next];
    next = (next + 1) % kQueue;
    pixels(t, p, n, first);
    if (spi_device_queue_trans(dev, &t.base, portMAX_DELAY) == ESP_OK) {
        ++inFlight;
        return;
    }
    // Refused (it shouldn't be: the strips are DMA memory, aligned): the ones before it go first, then this one polled,
    // as the library sends everything.
    drain();
    spi_device_polling_transmit(dev, &t.base);
}

}  // namespace

bool begin(int cs, int32_t hz, int colOffset, int rowOffset) {
    // As the library's own device (Arduino_ESP32QSPI::begin), with room to queue.
    spi_device_interface_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.command_bits = 8;
    cfg.address_bits = 24;
    cfg.mode = 0;
    cfg.clock_source = SPI_CLK_SRC_DEFAULT;
    cfg.clock_speed_hz = hz;
    cfg.spics_io_num = -1;   // the chip select is a plain GPIO, driven here and by the library
    cfg.flags = SPI_DEVICE_HALFDUPLEX;
    cfg.queue_size = kQueue;
    if (spi_bus_add_device(ESP32QSPI_SPI_HOST, &cfg, &dev) != ESP_OK) {
        dev = nullptr;
        return false;
    }
    size_t max = 0;
    if (spi_bus_get_max_transaction_len(ESP32QSPI_SPI_HOST, &max) != ESP_OK) max = 0;
    chunk = (max < kChunkMax ? max : kChunkMax) & ~static_cast<size_t>(3);
    if (chunk == 0) {
        spi_bus_remove_device(dev);
        dev = nullptr;
        return false;
    }
    csMask = 1u << (cs % 32);   // as the library's digitalPinToBitMask
    csSetReg = cs >= 32 ? GPIO_OUT1_W1TS_REG : GPIO_OUT_W1TS_REG;
    csClrReg = cs >= 32 ? GPIO_OUT1_W1TC_REG : GPIO_OUT_W1TC_REG;
    xOff = colOffset;
    yOff = rowOffset;
    return true;
}

void start(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* px) {
    finish();
    if (x != curX || y != curY || w != curW || h != curH) {   // as Arduino_CO5300::writeAddrWindow
        curX = x;
        curY = y;
        curW = w;
        curH = h;
        const uint16_t x0 = static_cast<uint16_t>(x + xOff), y0 = static_cast<uint16_t>(y + yOff);
        command16x2(kCaset, x0, static_cast<uint16_t>(x0 + w - 1));
        command16x2(kPaset, y0, static_cast<uint16_t>(y0 + h - 1));
    }
    command(kRamwr, nullptr, 0);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(px);
    size_t left = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;
    if (left == 0) return;   // the library would send no pixel transaction either
    if (left <= kPollMax) {
        pixels(cmdT, p, left, true);
        csLow();
        spi_device_polling_transmit(dev, &cmdT.base);
        csHigh();
        return;
    }
    csLow();
    selected = true;
    for (bool first = true; left; first = false) {
        const size_t n = left < chunk ? left : chunk;
        queue(p, n, first);
        p += n;
        left -= n;
    }
}

void finish() {
    if (!selected) return;
    drain();
    csHigh();
    selected = false;
}

}  // namespace panel
