#include "dma_copy.h"

#include <Arduino.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_private/gdma.h>
#include <hal/dma_types.h>

#include "dock_ui.h"

namespace dmacopy {

namespace {

// A band of kBandRows portrait rows is kBandRows panel columns of all 480 panel rows: 480 runs of 64 bytes, one a
// panel row (1600 bytes apart), each one whole 64-byte cache line and one DMA burst. The band buffer is read in
// 3840-byte pieces (a descriptor holds at most 4095).
constexpr int kPanelW = dock::H, kPanelH = dock::W;
constexpr int kRun = dock::kBandRows * 2;               // 64 bytes
constexpr int kBandBytes = kPanelH * kRun;              // 30720
constexpr int kTxPiece = 3840, kTxCount = kBandBytes / kTxPiece;
constexpr size_t kFbBytes = static_cast<size_t>(kPanelW) * kPanelH * 2;
static_assert(kRun == 64, "one DMA burst and one cache line per panel row");
static_assert(kBandBytes % kTxPiece == 0, "whole pieces");
// Far longer than a band takes (about 0.5 ms); past it the copy is redone by the CPU.
constexpr uint32_t kTimeoutUs = 20000;

gdma_channel_handle_t tx = nullptr, rx = nullptr;
dma_descriptor_t* txd = nullptr;   // kTxCount pieces of the band buffer
dma_descriptor_t* rxd = nullptr;   // kPanelH runs in the frame buffer
bool busy = false;
// The copy in flight, to redo it by the CPU if it doesn't land.
const uint16_t* pendBand = nullptr;
uint16_t* pendFb = nullptr;
int pendY0 = 0;
bool pendCw = true;
uint32_t fallbackCount = 0;

void cpuCopy(const uint16_t* band, uint16_t* fb, int y0, int rows, bool cw) {
    dock::copyBand(band, fb, y0, rows, cw);
    writeBack(fb);   // the cache's new lines out to PSRAM, so finish() can drop the frame buffer's lines safely
}

// Waits for the copy in flight; one that doesn't land is stopped and redone by the CPU.
void wait() {
    if (!busy) return;
    busy = false;
    volatile dma_descriptor_t* last = &rxd[kPanelH - 1];
    const uint32_t t0 = micros();
    while (!last->dw0.suc_eof) {
        if (micros() - t0 > kTimeoutUs) {
            gdma_stop(tx);
            gdma_stop(rx);
            gdma_reset(tx);
            gdma_reset(rx);
            ++fallbackCount;
            cpuCopy(pendBand, pendFb, pendY0, dock::kBandRows, pendCw);
            return;
        }
    }
}

void startCb(const uint16_t* band, uint16_t* fb, int y0, int rows, bool cw) { start(band, fb, y0, rows, cw); }
void finishCb(uint16_t* fb) { finish(fb); }
const dock::BandCopier kCopier = {startCb, finishCb};

}  // namespace

bool begin() {
    if (rx) return true;
    txd = static_cast<dma_descriptor_t*>(heap_caps_calloc(kTxCount, sizeof(dma_descriptor_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    rxd = static_cast<dma_descriptor_t*>(heap_caps_calloc(kPanelH, sizeof(dma_descriptor_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    gdma_channel_alloc_config_t tc = {};
    tc.direction = GDMA_CHANNEL_DIRECTION_TX;
    gdma_channel_alloc_config_t rc = {};
    rc.direction = GDMA_CHANNEL_DIRECTION_RX;
    bool ok = txd && rxd && gdma_new_ahb_channel(&tc, &tx) == ESP_OK;
    if (ok) {
        rc.sibling_chan = tx;
        ok = gdma_new_ahb_channel(&rc, &rx) == ESP_OK;
    }
    uint32_t mask = 0;
    ok = ok && gdma_get_free_m2m_trig_id_mask(tx, &mask) == ESP_OK && mask;
    if (ok) {
        gdma_trigger_t trig = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_M2M, 0);
        trig.instance_id = __builtin_ctz(mask);
        ok = gdma_connect(tx, trig) == ESP_OK && gdma_connect(rx, trig) == ESP_OK;
    }
    if (ok) {
        // Descriptors are set up by the CPU once and reused: no owner checks, no write-back of TX descriptors.
        gdma_strategy_config_t st = {};
        gdma_apply_strategy(tx, &st);
        gdma_apply_strategy(rx, &st);
        gdma_transfer_config_t txc = {};
        txc.max_data_burst_size = 32;   // internal RAM
        gdma_transfer_config_t rxc = {};
        rxc.max_data_burst_size = 64;   // PSRAM: one 64-byte burst a panel row
        rxc.access_ext_mem = true;
        ok = gdma_config_transfer(tx, &txc) == ESP_OK && gdma_config_transfer(rx, &rxc) == ESP_OK;
    }
    if (!ok) {
        if (rx) gdma_disconnect(rx), gdma_del_channel(rx);
        if (tx) gdma_disconnect(tx), gdma_del_channel(tx);
        rx = tx = nullptr;
        heap_caps_free(txd);
        heap_caps_free(rxd);
        txd = rxd = nullptr;
        return false;
    }
    for (int i = 0; i < kTxCount; ++i) {
        txd[i].dw0.size = kTxPiece;
        txd[i].dw0.length = kTxPiece;
        txd[i].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        txd[i].dw0.suc_eof = i == kTxCount - 1;
        txd[i].next = i == kTxCount - 1 ? nullptr : &txd[i + 1];
    }
    for (int r = 0; r < kPanelH; ++r) {
        rxd[r].dw0.size = kRun;
        rxd[r].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        rxd[r].next = r == kPanelH - 1 ? nullptr : &rxd[r + 1];
    }
    return true;
}

const dock::BandCopier* copier() { return rx ? &kCopier : nullptr; }

void start(const uint16_t* band, uint16_t* fb, int y0, int rows, bool cw) {
    wait();
    if (!rx || rows != dock::kBandRows) {
        cpuCopy(band, fb, y0, rows, cw);
        return;
    }
    // The band's panel columns (bands.h's mapping, as copyBand).
    const int col = cw ? y0 : kPanelW - y0 - rows;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(band);
    uint8_t* d = reinterpret_cast<uint8_t*>(fb + col);
    for (int i = 0; i < kTxCount; ++i) txd[i].buffer = const_cast<uint8_t*>(s + i * kTxPiece);
    for (int r = 0; r < kPanelH; ++r) {
        rxd[r].buffer = d + r * kPanelW * 2;
        rxd[r].dw0.length = 0;
        rxd[r].dw0.suc_eof = 0;   // the DMA sets it on the last run once the band has landed
    }
    pendBand = band;
    pendFb = fb;
    pendY0 = y0;
    pendCw = cw;
    __asm__ __volatile__("memw" ::: "memory");   // the descriptors are in RAM before the DMA reads them
    gdma_start(rx, reinterpret_cast<intptr_t>(rxd));
    gdma_start(tx, reinterpret_cast<intptr_t>(txd));
    busy = true;
}

void finish(uint16_t* fb) {
    wait();
    // The DMA wrote PSRAM behind the cache: lines of fb the cache still holds (read by the panel's refill when it was
    // on screen) are stale. Nothing has written fb through the cache since writeBack(), so none is dirty.
    esp_cache_msync(fb, kFbBytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

void writeBack(uint16_t* fb) {
    esp_cache_msync(fb, kFbBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

uint32_t fallbacks() { return fallbackCount; }

}  // namespace dmacopy
