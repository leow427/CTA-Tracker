// Checks src/panel_bus.cpp on a laptop against a model of what Arduino_GFX 1.6.9 put on the wire for each window
// (Arduino_CO5300::draw16bitBeRGBBitmap: writeAddrWindow, then Arduino_ESP32QSPI::writeBytes in 2 KB polled
// transactions). panel_bus.cpp is compiled unchanged against stand-ins for the SPI driver and the GPIO registers
// (tools/panel_bus_stub/), which record every transaction framed by the chip select and check the driver's rules:
// polled transactions never while queued ones are in flight, no more in flight than the queue holds, a descriptor
// never touched while in flight, the chip select high only with nothing in flight, nothing longer than the bus takes.
// Sends are windows like main.cpp's (even first row, odd last row; full strips, tiny runs, mixed, repeated windows),
// with the driver taking up to 20,460 bytes per transaction as on the device, refusing every third queued one (the
// polled fallback), and taking only 4,092 (more transactions in flight than one strip needs).
// Build and run (from firmware/handheld/mock):
//   c++ -std=c++17 -O2 -Itools/panel_bus_stub -Isrc tools/panel_bus_test.cpp src/panel_bus.cpp \
//       -o /tmp/panel_bus_test && /tmp/panel_bus_test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <vector>

#include "Arduino.h"
#include "driver/spi_master.h"
#include "panel_bus.h"
#include "soc/gpio_reg.h"

struct spi_device_t {
    int unused;
};

namespace {

constexpr int W = 466, kCs = 12;
constexpr uint32_t kCsMask = 1u << kCs;

// One transaction as it goes on the wire: its phases, their line modes, and the bytes.
struct Tx {
    int cmdBits = 0, addrBits = 0, dummyBits = 0;
    uint32_t cmd = 0;
    uint64_t addr = 0;
    bool cmdQuad = false, addrQuad = false, dataQuad = false;
    std::vector<uint8_t> data;
    bool operator==(const Tx& o) const {
        return cmdBits == o.cmdBits && addrBits == o.addrBits && dummyBits == o.dummyBits && cmd == o.cmd &&
               addr == o.addr && cmdQuad == o.cmdQuad && addrQuad == o.addrQuad && dataQuad == o.dataQuad &&
               data == o.data;
    }
};
using Frame = std::vector<Tx>;   // everything between the chip select going low and going high

std::vector<Frame> wire, want;
bool csLow = false;
int violations = 0;
spi_device_interface_config_t devCfg;
spi_device_t theDev;
struct InFlight {
    spi_transaction_t* t;
    spi_transaction_ext_t snap;
};
std::deque<InFlight> inFlight;
size_t maxLen = 20460;   // what the device's bus takes (5 DMA descriptors of 4,092 bytes)
int failEvery = 0, queueCalls = 0, maxInFlight = 0;

void violation(const char* what) {
    if (++violations <= 10) printf("FAIL: %s\n", what);
}

Tx decode(const spi_transaction_t* t) {
    const spi_transaction_ext_t* e = reinterpret_cast<const spi_transaction_ext_t*>(t);
    Tx x;
    x.cmdBits = (t->flags & SPI_TRANS_VARIABLE_CMD) ? e->command_bits : devCfg.command_bits;
    x.addrBits = (t->flags & SPI_TRANS_VARIABLE_ADDR) ? e->address_bits : devCfg.address_bits;
    x.dummyBits = (t->flags & SPI_TRANS_VARIABLE_DUMMY) ? e->dummy_bits : devCfg.dummy_bits;
    x.cmd = x.cmdBits ? t->cmd : 0;
    x.addr = x.addrBits ? t->addr : 0;
    x.dataQuad = t->flags & SPI_TRANS_MODE_QIO;
    x.cmdQuad = x.dataQuad && (t->flags & SPI_TRANS_MULTILINE_CMD);
    x.addrQuad = x.dataQuad && (t->flags & SPI_TRANS_MULTILINE_ADDR);
    if (t->length % 8) violation("a length that isn't whole bytes");
    const size_t n = t->length / 8;
    if (n > maxLen) violation("a transaction longer than the bus takes");
    if (t->flags & SPI_TRANS_USE_TXDATA) {
        if (n > 4) violation("more than 4 bytes of tx_data");
        x.data.assign(t->tx_data, t->tx_data + n);
    } else if (n) {
        const uint8_t* p = static_cast<const uint8_t*>(t->tx_buffer);
        if (!p) violation("no tx buffer");
        else if (reinterpret_cast<uintptr_t>(p) & 3) violation("a tx buffer not on a 4-byte boundary");
        else x.data.assign(p, p + n);
    }
    return x;
}

void onWire(const spi_transaction_t* t) {
    if (!csLow) violation("a transaction with the chip select high");
    if (wire.empty()) wire.push_back(Frame());
    wire.back().push_back(decode(t));
}

// The library's wire for one window, from Arduino_CO5300::writeAddrWindow (offset 6, its window cache, which begin()
// and fillScreen() leave at the whole screen) and Arduino_ESP32QSPI::writeC8D16D16, writeCommand and writeBytes.
int16_t libX = 0, libY = 0, libW = W, libH = W;

Tx libCommand(uint8_t c, const uint8_t* d, int n) {
    Tx x;
    x.cmdBits = 8;
    x.addrBits = 24;
    x.cmd = 0x02;
    x.addr = static_cast<uint64_t>(c) << 8;
    x.data.assign(d, d + n);
    return x;
}

void libDraw(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* px) {
    if (x != libX || w != libW || y != libY || h != libH) {
        libX = x;
        libY = y;
        libW = w;
        libH = h;
        const uint16_t xs = x + 6, ys = y, xe = xs + w - 1, ye = ys + h - 1;
        const uint8_t ca[4] = {uint8_t(xs >> 8), uint8_t(xs), uint8_t(xe >> 8), uint8_t(xe)};
        const uint8_t pa[4] = {uint8_t(ys >> 8), uint8_t(ys), uint8_t(ye >> 8), uint8_t(ye)};
        want.push_back({libCommand(0x2A, ca, 4)});
        want.push_back({libCommand(0x2B, pa, 4)});
    }
    want.push_back({libCommand(0x2C, nullptr, 0)});
    const uint8_t* p = reinterpret_cast<const uint8_t*>(px);
    uint32_t len = static_cast<uint32_t>(w) * h * 2;
    Frame f;
    for (bool first = true; len; first = false) {
        const uint32_t l = len >= 2048 ? 2048 : len;
        Tx t;
        t.dataQuad = true;
        if (first) {
            t.cmdBits = 8;
            t.addrBits = 24;
            t.cmd = 0x32;
            t.addr = 0x003C00;
        }
        t.data.assign(p, p + l);
        f.push_back(t);
        len -= l;
        p += l;
    }
    want.push_back(f);
}

// A data-only transaction continues the one before it on the wire (chip select still low, no phases of its own), so
// the library's 2 KB pieces and panel_bus's 16 KB ones compare as one.
std::vector<Frame> joined(const std::vector<Frame>& in) {
    std::vector<Frame> out;
    for (const Frame& f : in) {
        Frame g;
        for (const Tx& t : f) {
            if (!g.empty() && t.cmdBits == 0 && t.addrBits == 0 && t.dummyBits == 0 && t.dataQuad == g.back().dataQuad)
                g.back().data.insert(g.back().data.end(), t.data.begin(), t.data.end());
            else
                g.push_back(t);
        }
        out.push_back(g);
    }
    return out;
}

uint32_t rnd() {
    static uint32_t s = 12345;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

void send(int16_t x, int r0, int r1, int16_t rows, uint16_t* strip, int& sends) {
    panel::finish();   // as main.cpp: the send before is done first (StripSender's settle)
    const int16_t n = static_cast<int16_t>(r1 - r0 + 1);
    panel::start(x, static_cast<int16_t>(r0), rows, n, strip + r0 * rows);
    libDraw(x, static_cast<int16_t>(r0), rows, n, strip + r0 * rows);
    ++sends;
}

// Frames of runs as changedRuns emits them, through panel_bus and the library model; true if the wires match.
bool runFrames(const char* name) {
    wire.clear();
    want.clear();
    maxInFlight = 0;
    queueCalls = 0;
    const int before = violations;
    static uint16_t buf[2][W * 48] __attribute__((aligned(4)));
    int sends = 0;
    for (int frame = 0; frame < 400; ++frame) {
        for (int s = 0; s < 10; ++s) {
            const int k = s & 1, y0 = s * 48, rows = y0 + 48 <= W ? 48 : W - y0;
            for (int i = 0; i < W * rows; ++i) buf[k][i] = static_cast<uint16_t>(rnd());
            const int16_t x = static_cast<int16_t>(W - y0 - rows);
            const int mode = frame % 7;
            if (frame == 0 || mode == 3) {   // every row, as the first frame and after the profile
                send(x, 0, W - 1, static_cast<int16_t>(rows), buf[k], sends);
            } else {
                for (int r = static_cast<int>(rnd() % 40); r < W - 1;) {
                    const int r0 = r & ~1;
                    const int len = mode == 1 ? 2 : mode == 2 ? 2 + static_cast<int>(rnd() % 40) : 2 + static_cast<int>(rnd() % W);
                    int r1 = (r0 + len - 1) | 1;
                    if (r1 > W - 1) r1 = W - 1;
                    send(x, r0, r1, static_cast<int16_t>(rows), buf[k], sends);
                    if (rnd() % 5 == 0) {   // the same window again: no address commands this time
                        panel::finish();
                        for (int i = r0 * rows; i < (r1 + 1) * rows; ++i) buf[k][i] ^= 0x5a5a;
                        send(x, r0, r1, static_cast<int16_t>(rows), buf[k], sends);
                    }
                    r = r1 + 10 + static_cast<int>(rnd() % 60);
                }
            }
            panel::finish();   // the strip's buffer goes back only after this
            if (!inFlight.empty()) violation("a transaction still in flight after finish()");
        }
        panel::finish();   // the end of the frame: the brightness goes through the library after this
        if (!inFlight.empty() || csLow) violation("the bus busy at the end of a frame");
    }
    const std::vector<Frame> a = joined(wire), b = joined(want);
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
    printf("%s: %d sends, %zu chip-select frames (library %zu), up to %d in flight, wire %s\n", name, sends, a.size(),
           b.size(), maxInFlight, same ? "the same" : "DIFFERENT");
    if (!same) violation("the wire differs from the library's");
    return same && violations == before;
}

}  // namespace

// ---------- the stand-ins (tools/panel_bus_stub/) ----------

void mock_reg_write(uint32_t reg, uint32_t val) {
    if (val != kCsMask) violation("a register write that isn't the chip select alone");
    if (reg == GPIO_OUT_W1TC_REG) {
        if (csLow) violation("the chip select set low twice");
        csLow = true;
        wire.push_back(Frame());
    } else if (reg == GPIO_OUT_W1TS_REG) {
        if (!inFlight.empty()) violation("the chip select high with a transaction in flight");
        csLow = false;
    } else {
        violation("the wrong GPIO register");
    }
}

esp_err_t spi_bus_add_device(spi_host_device_t h, const spi_device_interface_config_t* c, spi_device_handle_t* d) {
    if (h != SPI2_HOST) violation("the wrong SPI host");
    devCfg = *c;
    // As Arduino_ESP32QSPI::begin's device: 8-bit command, 24-bit address, mode 0, 80 MHz, half duplex, no CS pin.
    if (c->command_bits != 8 || c->address_bits != 24 || c->dummy_bits || c->mode || c->spics_io_num != -1 ||
        c->flags != SPI_DEVICE_HALFDUPLEX || c->clock_speed_hz != 80000000 || c->queue_size < 1 || c->pre_cb ||
        c->post_cb)
        violation("a device set up differently from the library's");
    *d = &theDev;
    return ESP_OK;
}

esp_err_t spi_bus_remove_device(spi_device_handle_t) { return ESP_OK; }

esp_err_t spi_bus_get_max_transaction_len(spi_host_device_t, size_t* m) {
    *m = maxLen;
    return ESP_OK;
}

esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t* t, TickType_t) {
    ++queueCalls;
    if (failEvery && queueCalls % failEvery == 0) return ESP_FAIL;
    for (const InFlight& f : inFlight)
        if (f.t == t) violation("a descriptor queued while it was in flight");
    if (static_cast<int>(inFlight.size()) >= devCfg.queue_size) violation("more in flight than the queue holds");
    InFlight f;
    f.t = t;
    memcpy(&f.snap, t, sizeof(spi_transaction_ext_t));
    inFlight.push_back(f);
    if (static_cast<int>(inFlight.size()) > maxInFlight) maxInFlight = static_cast<int>(inFlight.size());
    onWire(t);   // queued transactions go out in order
    return ESP_OK;
}

esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t** out, TickType_t) {
    if (inFlight.empty()) {
        violation("waiting for a result with nothing in flight (it would wait forever)");
        return ESP_FAIL;
    }
    const InFlight f = inFlight.front();
    inFlight.pop_front();
    if (memcmp(&f.snap, f.t, sizeof(spi_transaction_ext_t)) != 0) violation("a descriptor changed while in flight");
    *out = f.t;
    return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t* t) {
    if (!inFlight.empty()) violation("a polled transaction while queued ones were in flight");
    onWire(t);
    return ESP_OK;
}

int main() {
    if (!panel::begin(kCs, 80000000, 6, 0)) {
        printf("FAIL: begin\n");
        return 1;
    }
    bool ok = runFrames("16 KB transactions");
    failEvery = 3;
    ok = runFrames("every third queued one refused") && ok;
    failEvery = 0;
    maxLen = 4092;
    ok = panel::begin(kCs, 80000000, 6, 0) && runFrames("4 KB transactions") && ok;
    if (!ok || violations) {
        printf("%d failures\n", violations);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
