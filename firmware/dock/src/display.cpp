#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <sdkconfig.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <new>

#include "dock_ui.h"
#include "pins.h"
#include "bands.h"
#include "dma_copy.h"

namespace display {

namespace {

constexpr int kPanelW = 800, kPanelH = 480;
uint8_t kGt911 = 0x5D;   // 0x14 if the reset strapping didn't take
constexpr uint8_t kCh422Mode = 0x24, kCh422Out = 0x38;   // CH422G command "addresses"

esp_lcd_panel_handle_t panel = nullptr;
uint16_t* fbs[2] = {};
int back = 1;
SemaphoreHandle_t frameDone;
QueueHandle_t touches;
bool gtOk = false;
uint8_t exio = 0;
// Bands of 32 portrait rows (25 a frame, bands.h) drawn in internal RAM: 30 KB, drawn several times faster than PSRAM.
// Two of them when the DMA copies bands (dma_copy.h): one is drawn while the other is copied out.
using dock::kBandRows;
uint16_t* band = nullptr;
uint16_t* band2 = nullptr;
const dock::BandCopier* copier = nullptr;
// Each frame is recorded once and played band by band (bands.h); the recording (about 450 KB) lives in PSRAM.
ui::Recording* rec = nullptr;
dock::BandMemo memo;   // what each frame buffer holds, band by band
uint32_t lastCopyUs = 0, lastWaitUs = 0, lastDrawUs = 0, switchTimeouts = 0;
int lastBandsCopied = 0, lastDrawnBands = 0;
Diag diagMode = Diag::Normal;
bool autoRecover = true;
uint32_t recoverCount = 0;
bool frozen = false;   // Freeze or Pattern: drawn once, then left alone

uint32_t usNow() { return micros(); }

void exioWrite(int pin, bool high) {
    exio = high ? (exio | (1 << pin)) : (exio & ~(1 << pin));
    Wire.beginTransmission(kCh422Out);
    Wire.write(exio);
    Wire.endTransmission();
}

bool gtRead(uint16_t reg, uint8_t* out, size_t n) {
    Wire.beginTransmission(kGt911);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(kGt911), static_cast<int>(n)) != static_cast<int>(n)) return false;
    for (size_t i = 0; i < n; ++i) out[i] = Wire.read();
    return true;
}

void gtClear() {
    Wire.beginTransmission(kGt911);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0);
    Wire.endTransmission();
}

// The panel's landscape point to the portrait UI (the inverse of rotateToPanel).
void toPortrait(int px, int py, int& x, int& y) {
#if DISPLAY_ROTATE_CW
    x = dock::W - 1 - py;
    y = px;
#else
    x = py;
    y = dock::H - 1 - px;
#endif
    x = x < 0 ? 0 : x >= dock::W ? dock::W - 1 : x;
    y = y < 0 ? 0 : y >= dock::H ? dock::H - 1 : y;
}

// The panel's driver reads the frame buffers into its bounce buffers a frame at a time and moves to a newly drawn
// buffer when it starts a frame; this fires when it has finished reading one, so after a switch the old buffer is
// free once this has fired.
// The panel's frame period at the configured clock and porches; a frame-complete event later than this by more
// than a bounce buffer's time means the refill interrupt ran late (the panel then shows stale or shifted lines).
volatile int64_t lastDoneUs = 0;
volatile uint32_t doneCount = 0, lateCount = 0, maxGapUs = 0;
// A refill that ran late (a flash write stopped the cache, so the panel's interrupt couldn't run) leaves the driver
// filling the bounce buffer the DMA is sending: with CONFIG_LCD_RGB_RESTART_IN_VSYNC (this core's default) its count
// of DMA EOFs is never reset, so the two stay out of step and the start of every 20 lines shows stale pixels (the
// slivers in the status strip) until another stall flips them back. Recreating the panel resets it.
volatile bool recoverWanted = false;
volatile uint32_t holdoffFrames = 0;   // frames to ignore after a (re)start
volatile bool bounceMode = true;       // the panel runs with bounce buffers (a refill can fall out of step)
uint32_t framePeriodUs = 0, lateSlackUs = 0;

bool IRAM_ATTR onFrameDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
    const int64_t t = esp_timer_get_time();
    if (holdoffFrames) {
        --holdoffFrames;
    } else if (lastDoneUs) {
        const uint32_t gap = static_cast<uint32_t>(t - lastDoneUs);
        if (gap > maxGapUs) maxGapUs = gap;
        if (gap > framePeriodUs + lateSlackUs || gap + lateSlackUs < framePeriodUs) {
            ++lateCount;
            // Without bounce buffers the event comes off the DMA and skips a frame whenever a buffer switch is
            // linked in, so a gap there says nothing about the picture: no restart.
            if (bounceMode) recoverWanted = true;
        }
    }
    lastDoneUs = t;
    ++doneCount;
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(frameDone, &woke);
    return woke == pdTRUE;
}

// The GT911 sets "buffer ready" once per scan. Between scans (or on a failed read) the last report stands, so a held
// finger never reads as a release.
void touchTask(void*) {
    bool down = false;
    int lx = 0, ly = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t status;
        if (!gtRead(0x814E, &status, 1) || !(status & 0x80)) continue;
        const int n = status & 0x0F;
        uint8_t p[8];
        Touch t{};
        t.ms = millis();
        t.fingers = static_cast<uint8_t>(n);
        if (n > 0 && n <= 5 && gtRead(0x814F, p, 8)) {
            int x, y;
            toPortrait(p[1] | (p[2] << 8), p[3] | (p[4] << 8), x, y);
            t.kind = down ? Touch::Move : Touch::Down;
            t.x = static_cast<int16_t>(x);
            t.y = static_cast<int16_t>(y);
            if (!down || x != lx || y != ly) xQueueSend(touches, &t, 0);
            down = true;
            lx = x; ly = y;
        } else if (n == 0 && down) {
            t.kind = Touch::Up;
            t.x = static_cast<int16_t>(lx);
            t.y = static_cast<int16_t>(ly);
            xQueueSend(touches, &t, 0);
            down = false;
        }
        gtClear();
    }
}

// The panel's timing: the build's (index 0) and the variants the diagnostic key "6" steps through.
struct Timing {
    const char* name;
    uint32_t pclkHz;
    uint8_t hpw, hbp, hfp, vpw, vbp, vfp;
    bool pclkNeg;
    int bounceLines;
};
const Timing kTimings[] = {
    {"build: 14 MHz, falling edge, 20-line bounce", LCD_PCLK_HZ, 4, 8, 8, 4, 8, 8, true, 20},
    {"Waveshare demo: 16 MHz, falling edge, 10-line bounce", 16000000, 4, 8, 8, 4, 8, 8, true, 10},
    {"16 MHz, rising edge, 20-line bounce", 16000000, 4, 8, 8, 4, 8, 8, false, 20},
    {"12 MHz, falling edge, 20-line bounce", 12000000, 4, 8, 8, 4, 8, 8, true, 20},
    // No bounce buffers: the DMA reads the PSRAM frame buffers itself, with no refill interrupt to fall out of
    // step; an underrun is put right at the next frame (CONFIG_LCD_RGB_RESTART_IN_VSYNC).
    {"14 MHz, falling edge, no bounce", 14000000, 4, 8, 8, 4, 8, 8, true, 0},
    {"16 MHz, falling edge, no bounce", 16000000, 4, 8, 8, 4, 8, 8, true, 0},
};
constexpr int kTimingCount = sizeof(kTimings) / sizeof(kTimings[0]);
// The timing the dock starts with: T0, 14 MHz with 20-line bounce buffers (about 1.2 ms per refill), on the high_perf
// SDK (platformio.ini). Espressif's board file uses 16 MHz and 10 lines (T1), which still glitched briefly in Leo's
// test (2026-11-12); its drift guide says to grow the bounce buffers next. T4 and T5 (no bounce buffers): diag only.
constexpr int kBootTiming = 0;
int timingIndex = kBootTiming;

void resetHashes() { memo.forget(); }

// Creates the RGB panel with two PSRAM frame buffers (black) and the frame-complete event, on the calling core
// (its interrupts run there).
bool createPanel(const Timing& t) {
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = t.pclkHz;
    cfg.timings.h_res = kPanelW;
    cfg.timings.v_res = kPanelH;
    cfg.timings.hsync_pulse_width = t.hpw;
    cfg.timings.hsync_back_porch = t.hbp;
    cfg.timings.hsync_front_porch = t.hfp;
    cfg.timings.vsync_pulse_width = t.vpw;
    cfg.timings.vsync_back_porch = t.vbp;
    cfg.timings.vsync_front_porch = t.vfp;
    cfg.timings.flags.pclk_active_neg = t.pclkNeg;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 2;
    // DMA from two internal bounce buffers (20 lines: 32 KB each, about 1.2 ms of scan at 14 MHz), so a refill held
    // up by other PSRAM or flash traffic still lands before the panel needs it.
    cfg.bounce_buffer_size_px = kPanelW * t.bounceLines;
    cfg.dma_burst_size = 64;
    cfg.hsync_gpio_num = LCD_HSYNC;
    cfg.vsync_gpio_num = LCD_VSYNC;
    cfg.de_gpio_num = LCD_DE;
    cfg.pclk_gpio_num = LCD_PCLK;
    cfg.disp_gpio_num = -1;
    const int data[16] = LCD_DATA_PINS;
    for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = data[i];
    cfg.flags.fb_in_psram = 1;
    if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK || esp_lcd_panel_reset(panel) != ESP_OK ||
        esp_lcd_panel_init(panel) != ESP_OK) {
        Serial.println("display: RGB panel init failed");
        panel = nullptr;
        return false;
    }
    void *a = nullptr, *b = nullptr;
    esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &a, &b);
    fbs[0] = static_cast<uint16_t*>(a);
    fbs[1] = static_cast<uint16_t*>(b);
    if (!fbs[0] || !fbs[1]) { Serial.println("display: no frame buffers"); return false; }
    memset(fbs[0], 0, kPanelW * kPanelH * 2);
    memset(fbs[1], 0, kPanelW * kPanelH * 2);
    dmacopy::writeBack(fbs[0]);   // the CPU wrote them: out of the cache before any DMA copy (dma_copy.h)
    dmacopy::writeBack(fbs[1]);
    back = 1;
    resetHashes();
    {
        const uint32_t lineClk = kPanelW + t.hpw + t.hbp + t.hfp, lines = kPanelH + t.vpw + t.vbp + t.vfp;
        framePeriodUs = static_cast<uint32_t>(static_cast<uint64_t>(lineClk) * lines * 1000000ULL / t.pclkHz);
        // A bounce buffer's time (20 lines' without bounce buffers: there the event comes off the DMA, a little loose).
        lateSlackUs = static_cast<uint32_t>(static_cast<uint64_t>(lineClk) * (t.bounceLines ? t.bounceLines : 20) * 1000000ULL / t.pclkHz);
    }
    lastDoneUs = 0;
    holdoffFrames = 3;
    bounceMode = t.bounceLines > 0;
    recoverWanted = false;
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_frame_buf_complete = onFrameDone;
    esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);
    return true;
}

}  // namespace

bool beginHere() {
    frameDone = xSemaphoreCreateBinary();
    touches = xQueueCreate(32, sizeof(Touch));
    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    Wire.beginTransmission(kCh422Mode);
    Wire.write(0x01);   // IO0–7 as outputs
    Wire.endTransmission();
    // The GT911's reset with INT held low selects address 0x5D.
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);
    exio = (1 << EXIO_LCD_RST) | (1 << EXIO_SD_CS);
    exioWrite(EXIO_TOUCH_RST, false);
    delay(20);
    exioWrite(EXIO_TOUCH_RST, true);
    delay(60);
    pinMode(TOUCH_INT, INPUT);
    delay(50);

    if (!createPanel(kTimings[kBootTiming])) return false;
    band = static_cast<uint16_t*>(heap_caps_malloc(dock::W * kBandRows * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!band) { Serial.println("display: no RAM for the band"); return false; }
    // Without them every band is copied by the CPU, as before (slower, same pixels).
    band2 = static_cast<uint16_t*>(heap_caps_malloc(dock::W * kBandRows * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (band2 && dmacopy::begin()) {
        copier = dmacopy::copier();
    } else {
        heap_caps_free(band2);   // unused without the DMA: its 30 KB back to the heap
        band2 = nullptr;
        Serial.println("display: no DMA band copies (CPU copies)");
    }
    Serial.printf("display: internal RAM free %u after the band buffers\n", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    // Without it every band is drawn by calling the screens' code, as before (slower, same pixels).
    if (void* p = heap_caps_malloc(sizeof(ui::Recording), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) rec = new (p) ui::Recording();
    else Serial.println("display: no PSRAM for the frame recording");
    exioWrite(EXIO_BACKLIGHT, true);

    uint8_t id[4] = {};
    gtOk = gtRead(0x8140, id, 4);
    if (!gtOk) { kGt911 = 0x14; gtOk = gtRead(0x8140, id, 4); }
    Serial.printf("display: panel up, timing %d (%s); touch %s\n", timingIndex, kTimings[timingIndex].name, gtOk ? "GT911 found" : "not answering");
    // Proof the high_perf SDK is the one linked: without XIP from PSRAM a flash write stalls the bounce-buffer refill.
#if defined(CONFIG_SPIRAM_XIP_FROM_PSRAM) || (defined(CONFIG_SPIRAM_FETCH_INSTRUCTIONS) && defined(CONFIG_SPIRAM_RODATA))
    const bool xip = true;
#else
    const bool xip = false;
#endif
#ifdef CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE
    const int cacheLine = CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE;
#else
    const int cacheLine = 0;
#endif
#ifdef CONFIG_LCD_RGB_ISR_IRAM_SAFE
    const bool isrIram = true;
#else
    const bool isrIram = false;
#endif
    Serial.printf("display: SDK %s, XIP from PSRAM %s, data cache line %d B, panel ISR IRAM-safe %s\n", esp_get_idf_version(),
                  xip ? "on" : "OFF", cacheLine, isrIram ? "yes" : "no");
    // Core 0 at a priority above the network task: a slow TLS handshake never delays a touch sample.
    xTaskCreatePinnedToCore(touchTask, "touch", 3072, nullptr, 3, nullptr, 0);
    return true;
}

bool render(void (*draw)(ui::Canvas& cv, void* ctx), void* ctx) {
    if (!panel || !band) return false;
    if (recoverWanted && autoRecover) {
        recoverWanted = false;
        recover();
    }
    if (frozen) return false;
    if (diagMode == Diag::Freeze || diagMode == Diag::Pattern) frozen = true;   // this frame (or the pattern) stays
    if (diagMode == Diag::Single) {
        // Straight into the buffer the panel is scanning, no swap: what a single-buffered panel would show.
        const int front = back ^ 1;
        for (int y0 = 0; y0 < dock::H; y0 += kBandRows) {
            ui::Canvas cv = dock::bandCanvas(band, y0, kBandRows, DISPLAY_ROTATE_CW);
            draw(cv, ctx);
            dock::copyBand(band, fbs[front], y0, kBandRows, DISPLAY_ROTATE_CW);
        }
        dmacopy::writeBack(fbs[front]);   // CPU writes: out of the cache before a DMA copy lands there (dma_copy.h)
        resetHashes();
        return false;   // no switch, so no wait for the panel
    }
    const int copies = diagMode == Diag::Pattern ? 2 : 1;   // the pattern goes into both buffers, then nothing moves
    for (int copy = 0; copy < copies; ++copy) {
    // The frame into the hidden buffer (bands.h): recorded once, and only the bands whose drawing isn't the one this
    // buffer already holds are drawn and copied. The pattern (which prints the time each band is drawn) is drawn band
    // by band directly, as before. The switch and the wait below are as they were: a buffer is only ever drawn into
    // while the panel isn't reading it.
    dock::FrameStats st;
    dock::drawFrame(draw, ctx, diagMode == Diag::Pattern ? nullptr : rec, memo, back, band, fbs[back], DISPLAY_ROTATE_CW,
                    usNow, st, band2, copier);
    const uint32_t w0 = micros();
    // Without bounce buffers the DMA reads the frame buffer from PSRAM itself, past the CPU's cache: draw_bitmap
    // writes the cache back for the rectangle it's given (the whole frame) before the switch.
    // Switch to the new buffer (one of the panel's own: no copy, it takes effect at the start of the next frame), then
    // wait until a frame read wholly after the switch was asked for has finished: only then is the old buffer no
    // longer being read, so drawing into it can't tear. An event from before the request is dropped first.
    esp_lcd_panel_draw_bitmap(panel, 0, 0, kPanelW, kPanelH, fbs[back]);
    xSemaphoreTake(frameDone, 0);
    if (xSemaphoreTake(frameDone, pdMS_TO_TICKS(80)) != pdTRUE) ++switchTimeouts;
    back ^= 1;
    lastCopyUs = st.copyUs;
    lastDrawUs = st.drawUs;
    lastBandsCopied = st.copied;
    lastDrawnBands = st.drawn;
    lastWaitUs = micros() - w0;
    }
    return true;
}

// Only with bounce buffers: without them nothing falls out of step for good (the DMA restarts each frame).
void requestRecover() { if (bounceMode) recoverWanted = true; }
void setAutoRecover(bool on) { autoRecover = on; }
bool autoRecovering() { return autoRecover; }
uint32_t recoveries() { return recoverCount; }
uint32_t dmaFallbacks() { return dmacopy::fallbacks(); }

void recover() {
    if (!panel) return;
    const uint32_t t0 = millis();
    esp_lcd_panel_del(panel);
    panel = nullptr;
    fbs[0] = fbs[1] = nullptr;
    const bool ok = createPanel(kTimings[timingIndex]);
    frozen = false;   // the frozen picture (or pattern) went with the old buffers: it's drawn again
    ++recoverCount;
    Serial.printf("display: panel restarted (%u ms)%s\n", static_cast<unsigned>(millis() - t0), ok ? "" : " FAILED");
}

void setDiag(Diag d) {
    diagMode = d;
    frozen = false;
    resetHashes();
}
Diag diag() { return diagMode; }

int timingCount() { return kTimingCount; }
int timing() { return timingIndex; }
int bootTiming() { return kBootTiming; }
const char* timingName(int i) { return kTimings[i].name; }

// Recreates the panel with another timing. Runs on the loop's core (1), so the panel's interrupts stay there.
bool setTiming(int i) {
    if (i < 0 || i >= kTimingCount || !panel) return false;
    esp_lcd_panel_del(panel);
    panel = nullptr;
    fbs[0] = fbs[1] = nullptr;
    timingIndex = i;
    const bool ok = createPanel(kTimings[i]);
    frozen = false;
    Serial.printf("display: timing %d: %s%s\n", i, kTimings[i].name, ok ? "" : " FAILED");
    return ok;
}

// The panel's interrupts (the bounce buffers' refill out of PSRAM) run on the core that created it. On core 0 they
// were held up by the Wi-Fi driver and TLS there, and a late refill shows stale lines: the picture shook and tore
// (Leo, 2026-11-12; serial "scan:" counted 10–15 late frames every 5 s during fetches). So it's core 1, the frame
// loop's core: drawing is slower (the interrupts take their share of it), but the refills run on time.
bool begin() {
    struct Job { SemaphoreHandle_t done; bool ok; } job = {xSemaphoreCreateBinary(), false};
    xTaskCreatePinnedToCore([](void* p) {
        Job& j = *static_cast<Job*>(p);
        j.ok = beginHere();
        xSemaphoreGive(j.done);
        vTaskDelete(nullptr);
    }, "panel", 4096, &job, 5, nullptr, 1);
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return job.ok;
}

bool nextTouch(Touch& t) { return touches && xQueueReceive(touches, &t, 0) == pdTRUE; }
bool touchOk() { return gtOk; }
uint32_t timeouts() { return switchTimeouts; }

void scanStats(uint32_t& frames, uint32_t& late, uint32_t& maxGapUs_, uint32_t& periodUs) {
    frames = doneCount;
    late = lateCount;
    maxGapUs_ = maxGapUs;
    periodUs = framePeriodUs;
    doneCount = lateCount = maxGapUs = 0;
}

void backlight(bool on) { exioWrite(EXIO_BACKLIGHT, on); }

void dumpFrame() {
    if (!panel) return;
    const uint16_t* fb = fbs[back ^ 1];   // the one on screen
    // Lines of run-length pairs (count, color) in hex, "F:<first pixel>:" first; whole lines per write, so the network task's log
    // lines can come between them but never inside one.
    Serial.printf("FRAME %d %d\n", kPanelW, kPanelH);
    static const char kHex[] = "0123456789abcdef";
    char line[2 + 9 + 48 * 8 + 2];
    int n = 0;
    auto put16 = [&](uint16_t v) { for (int sh = 12; sh >= 0; sh -= 4) line[n++] = kHex[(v >> sh) & 15]; };
    const int total = kPanelW * kPanelH;
    for (int i = 0; i < total;) {
        if (n == 0) {   // "F:" and the line's first pixel, so a lost line shows as a gap
            line[n++] = 'F'; line[n++] = ':';
            put16(static_cast<uint16_t>(i >> 16)); put16(static_cast<uint16_t>(i));
            line[n++] = ':';
        }
        const uint16_t c = fb[i];
        int run = 1;
        while (i + run < total && fb[i + run] == c && run < 65535) ++run;
        put16(static_cast<uint16_t>(run));
        put16(c);
        i += run;
        if (n >= 2 + 9 + 48 * 8 || i >= total) {
            line[n++] = '\n';
            Serial.write(reinterpret_cast<const uint8_t*>(line), n);
            n = 0;
        }
    }
    Serial.println("END");
}
int lastBands() { return lastBandsCopied; }
int lastBandsDrawn() { return lastDrawnBands; }
void lastTimes(uint32_t& drawUs, uint32_t& copyUs, uint32_t& waitUs) {
    drawUs = lastDrawUs;
    copyUs = lastCopyUs;
    waitUs = lastWaitUs;
}

}  // namespace display
