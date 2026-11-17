// The handheld on the device: display (CO5300 over QSPI), touch (CST9217), and the frame loop. The live build
// (HANDHELD_LIVE, env:live) feeds the screens real CTA data from the network task (live.*) and reads the power chip
// (power.*); the mock build simulates arrivals (mock_data.*).
//
// Frames are drawn in strips of screen columns into internal RAM (fast, unlike PSRAM), already laid out the way the
// panel wants them: the panel is mounted a quarter turn off, so a strip of screen columns is a band of whole panel
// rows, rotated 90 degrees right and byte-swapped. Each panel row is hashed, and only the rows that changed since the
// last frame are sent, from a task on core 0 while core 1 draws the next strip. The panel scans its rows (the screen's
// columns) in the same order, and each send waits until the scan has passed its rows (the panel's TE line, GPIO13),
// so a whole frame shows on one scan and none is caught half written (todo B1: strips of screen rows ran across the
// scan, and every frame showed with a slanted seam while the list moved). Touch is sampled on its
// own task every 8 ms, so gestures get real timings; the touch panel reads a half turn from the display, so a
// touch at (tx, ty) is screen point (W-1-ty, tx).
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include "clock_ms.h"
#include "cpu_clock.h"
#include "json_heap.h"
#include "cst9217.h"
#include "device_hooks.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mock_data.h"
#include "row_hash.h"
#include "touch_filter.h"
#include "ui.h"
#ifdef HANDHELD_LIVE
#include <esp_mac.h>

#include "ble_setup.h"
#include "card_theme.h"
#include "link.h"
#include "live.h"
#include "live_model.h"
#include "pet_palette.h"
#include "pet_sprites.h"
#include "power.h"
#include "cta_config.h"
#endif
#ifdef POWER_LOG
#include "power_log.h"
#include "stress.h"
#endif

using std::max;
using std::min;

namespace {

// Waveshare ESP32-S3-Touch-AMOLED-1.75 pins (from Waveshare's pin_config.h).
constexpr int kLcdCs = 12, kLcdSclk = 38, kLcdD0 = 4, kLcdD1 = 5, kLcdD2 = 6, kLcdD3 = 7, kLcdRst = 39;
constexpr int kLcdTe = 13;   // the schematic's LCD_TE; Waveshare's own examples leave it unused
constexpr int kSda = 15, kScl = 14, kTpInt = 11, kTpRst = 40;
constexpr int W = ui::W;
constexpr uint8_t kFullBrightness = 220;
uint8_t fullBrightness = kFullBrightness;   // the app's screen brightness (config.settings.screen) of kFullBrightness

esp_lcd_panel_io_handle_t lcdIo;
esp_lcd_panel_handle_t panel;
SemaphoreHandle_t lcdDone;   // given when a draw's last pixel has gone out (draw_bitmap only queues the transfer)
i2c_master_bus_handle_t i2c;
bool touchOk = false;

uint16_t* scratch;
// On the heap, so in PSRAM (blocks over 4 KB go there, and PSRAM is up before constructors run): its 46 KB of
// cards would crowd internal RAM, which the live build needs for Wi-Fi and TLS.
mock::Model& model = *new mock::Model();
ui::App app;
uint32_t startUnix = 0, startMs = 0;
#ifdef HANDHELD_LIVE
live::Snapshot* snap;   // the newest from the network task (PSRAM); the model's pips point into it
uint32_t snapVersion = 0;
power::State pw;
#endif

// The build time, read as Chicago local time, is the mock's clock.
uint32_t buildUnix() {
    static const char* const kMon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
    const int mon = static_cast<int>((strstr(kMon, m) - kMon) / 3) + 1;
    const int day = atoi(__DATE__ + 4), year = atoi(__DATE__ + 7);
    const int hh = atoi(__TIME__), mm = atoi(__TIME__ + 3), ss = atoi(__TIME__ + 6);
    int y = year - (mon <= 2);
    const int era = y / 400, yoe = y - era * 400;
    const int doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long days = era * 146097L + yoe * 365L + yoe / 4 - yoe / 100 + doy - 719468L;
    const uint32_t naive = static_cast<uint32_t>(days * 86400L + hh * 3600L + mm * 60L + ss);
    for (int off : {6, 5})   // CST, then CDT
        if (mock::localMinutes(naive + off * 3600) == hh * 60 + mm) return naive + off * 3600;
    return naive + 6 * 3600;
}

// ---------- sending: changed panel rows, from core 0 ----------

constexpr int kStripCols = 48;                                     // screen columns (panel rows) per strip
constexpr int kStrips = (W + kStripCols - 1) / kStripCols;         // 10; the last is 34 columns
constexpr int kMergeGap = 8;                                       // unchanged rows worth sending to save a window

// ---------- the panel (esp_lcd_co5300 over QSPI) ----------

// Arduino_GFX's Arduino_CO5300 init (GFX Library for Arduino 1.6.9, co5300_init_operations, then begin()'s
// invertDisplay(false) and setRotation(0)), so the panel state is the same. The driver sends MADCTL 0x00 and
// COLMOD 0x55 before these.
const uint8_t k00[] = {0x00}, k80[] = {0x80}, k55[] = {0x55}, k20[] = {0x20}, kFF[] = {0xFF}, kD0[] = {0xD0};
const co5300_lcd_init_cmd_t kLcdInit[] = {
    {0x11, nullptr, 0, 120},   // sleep out
    {0xFE, k00, 1, 0},         // command page 0
    {0xC4, k80, 1, 0},         // SPI mode control
    {0x3A, k55, 1, 0},         // 16 bits a pixel
    {0x53, k20, 1, 0},         // brightness control on
    {0x63, kFF, 1, 0},         // HBM brightness
    {0x29, nullptr, 0, 0},     // display on
    {0x51, kD0, 1, 0},         // brightness
    {0x58, k00, 1, 10},        // high contrast off
    {0x20, nullptr, 0, 0},     // inversion off
    {0x36, k00, 1, 0},         // MADCTL: RGB, no flips
    {0x35, k00, 1, 0},         // TE on, vertical blanking only: paces the sends (todo B1)
};

bool IRAM_ATTR colorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(lcdDone, &woke);
    return woke == pdTRUE;
}

// A window of big-endian RGB565 pixels, as Arduino_GFX's draw16bitBeRGBBitmap took them; returns once they're sent,
// so the buffer is free again.
void lcdDraw(int x, int y, int w, int h, const uint16_t* px) {
    if (esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, px) == ESP_OK) xSemaphoreTake(lcdDone, portMAX_DELAY);
}

// The panel's brightness command (0x51), framed as the CO5300 driver frames commands on QSPI.
void lcdBrightness(uint8_t v) { esp_lcd_panel_io_tx_param(lcdIo, 0x02 << 24 | 0x51 << 8, &v, 1); }

constexpr int kStripBytes = W * 48 * 2;   // a strip buffer (kStripCols, below); the largest transfer

bool lcdBegin() {
    lcdDone = xSemaphoreCreateBinary();
    spi_bus_config_t bus = {};
    bus.data0_io_num = kLcdD0;
    bus.data1_io_num = kLcdD1;
    bus.sclk_io_num = kLcdSclk;
    bus.data2_io_num = kLcdD2;
    bus.data3_io_num = kLcdD3;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bus.max_transfer_sz = kStripBytes;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = kLcdCs;
    io.dc_gpio_num = -1;
    io.spi_mode = 0;
    io.pclk_hz = 80 * 1000 * 1000;   // as Arduino_GFX ran it
    io.trans_queue_depth = 10;
    io.on_color_trans_done = colorDone;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = 1;
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io, &lcdIo) != ESP_OK) return false;
    co5300_vendor_config_t vendor = {};
    vendor.init_cmds = kLcdInit;
    vendor.init_cmds_size = sizeof(kLcdInit) / sizeof(kLcdInit[0]);
    vendor.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t dev = {};
    dev.reset_gpio_num = kLcdRst;
    dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev.bits_per_pixel = 16;
    dev.vendor_config = &vendor;
    if (esp_lcd_new_panel_co5300(lcdIo, &dev, &panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) return false;
    esp_lcd_panel_set_gap(panel, 6, 0);   // Arduino_CO5300's column offset
    return true;
}

uint16_t* stripBuf[2];            // DMA-capable internal RAM; the bus sends straight from it
SemaphoreHandle_t stripFree[2];
uint32_t rowHash[W];              // per panel row (= screen column): what the panel shows
bool sendAll = true;

// ---------- the panel's scan (TE) ----------

// TE is high while the panel is between scans (mode 0x35 00: vertical blanking only), so its falling edge starts a
// scan of panel row 0. The interrupt keeps that time, the scan period, and how long TE stays high.
volatile uint32_t teFall = 0, tePeriod = 0, teHigh = 0, teRise = 0;
volatile uint32_t lateJobs = 0;   // sends that missed their scan (would tear); in the stats line
// A moving frame starts drawing this long before a scan starts, and a frame whose first send comes this late in a scan
// is timed against the next one: its first strips are ready when the scan passes them.
constexpr uint32_t kLeadUs = 2500;

void IRAM_ATTR onTe(void*) {
    const uint32_t t = static_cast<uint32_t>(esp_timer_get_time());
    if ((REG_READ(GPIO_IN_REG) >> kLcdTe) & 1) {   // a register read: the ISR stays in IRAM
        teRise = t;
    } else {
        if (teFall) tePeriod = t - teFall;
        teHigh = t - teRise;
        teFall = t;
    }
}

void waitUntil(uint32_t t) {
    for (int32_t d; (d = static_cast<int32_t>(t - nowUs())) > 0;) {
        if (d > 2000) vTaskDelay(pdMS_TO_TICKS(d / 1000 - 1));   // sleep, leaving the core to other tasks
        else esp_rom_delay_us(d);
    }
}

// Holds a send of panel rows [y0, y1) until the scan has passed them, so they show on the next scan whole. The first
// send of a frame anchors it to a scan; every later one must land before that scan's successor reaches its rows, or
// that scan shows the frame half new (counted in lateJobs).
void paceSend(int y0, int y1, uint32_t& anchor) {
    constexpr int kMargin = 16;   // panel rows of slack either side of the scan
    const uint32_t period = tePeriod, fall = teFall, now = nowUs();
    if (!period || now - fall > 4 * period) { anchor = 0; return; }   // no TE: send at once, as before
    const uint32_t high = teHigh < period / 4 ? teHigh : 0;
    const float line = static_cast<float>(period - high) / W;        // microseconds per panel row
    if (!anchor) anchor = now - fall >= period - kLeadUs ? fall + period : fall;
    waitUntil(anchor + static_cast<int32_t>((y1 + kMargin) * line));
    const int32_t over = static_cast<int32_t>(nowUs() - anchor - period) - static_cast<int32_t>((y0 - kMargin) * line);
    if (over > 0) lateJobs = lateJobs + 1;
}

struct Job {
    const uint16_t* px;   // nullptr: end of frame (apply the brightness)
    int16_t x, y, w, h;   // panel window
    int8_t release;       // the strip buffer this job finishes with, or -1
};
QueueHandle_t jobs;
volatile uint8_t wantBrightness = kFullBrightness;
volatile uint32_t sentPx = 0, sendUs = 0;
// Frames queued by the loop and finished by the sender (their end-of-frame job), so the loop can tell when its last
// frame is all on the panel.
volatile uint32_t framesQueued = 0, framesSent = 0;

void sendTask(void*) {
    uint8_t brightnessNow = 0;
    uint32_t anchor = 0;   // the scan this frame is timed against (paceSend)
    Job j;
    for (;;) {
        xQueueReceive(jobs, &j, portMAX_DELAY);
        if (j.px) {
            paceSend(j.y, j.y + j.h, anchor);
            const uint32_t t = nowUs();
            lcdDraw(j.x, j.y, j.w, j.h, j.px);
            sendUs += nowUs() - t;
            sentPx += j.w * j.h;
#ifdef POWER_LOG
            plog::sent(j.w * j.h);
#endif
        } else {
            if (wantBrightness != brightnessNow) {
                brightnessNow = wantBrightness;
                lcdBrightness(brightnessNow);
            }
#ifdef POWER_LOG
            plog::frameDone(static_cast<uint16_t>(j.x));   // an end-of-frame job carries the frame's number in x
#endif
            framesSent = framesSent + 1;
            anchor = 0;
        }
        if (j.release >= 0) xSemaphoreGive(stripFree[j.release]);
    }
}

#ifdef POWER_LOG
uint16_t frameSeq = 0;
#endif

// The end of a frame: the brightness to apply once it's all on the panel.
void queueEnd() {
    wantBrightness = static_cast<uint8_t>(fullBrightness * ui::brightness(app, model));
#ifdef POWER_LOG
    plog::frameQueued(frameSeq);
    const Job end{nullptr, static_cast<int16_t>(frameSeq), 0, 0, 0, -1};
#else
    const Job end{nullptr, 0, 0, 0, 0, -1};
#endif
    framesQueued = framesQueued + 1;
    xQueueSend(jobs, &end, portMAX_DELAY);
}

// Draws the frame strip by strip and queues the changed rows. Returns the microseconds spent drawing.
uint32_t drawFrame(uint32_t ms) {
    uint32_t busy = 0;
#ifdef POWER_LOG
    ++frameSeq;
    const bool lit = plog::litWanted();
#endif
    for (int s = 0; s < kStrips; ++s) {
        const int k = s & 1, x0 = s * kStripCols, x1 = min(W, x0 + kStripCols);
        xSemaphoreTake(stripFree[k], portMAX_DELAY);   // its last send has finished
        const uint32_t t = nowUs();
        uint16_t* buf = stripBuf[k];
        ui::Canvas cv = ui::Canvas::panelWindow(buf, W, x0, x1, 0, W);
        ui::draw(cv, app, model, ms, scratch);
#ifdef POWER_LOG
        if (lit) plog::litStrip(buf, W * (x1 - x0));
#endif
        // Panel window: whole panel rows (screen columns) [x0, x1), the ones that changed, in runs.
        int runStart = -1, runEnd = -1;
        Job pending{nullptr, 0, 0, 0, 0, -1};
        auto flush = [&]() {
            if (runStart < 0) return;
            if (pending.px) xQueueSend(jobs, &pending, portMAX_DELAY);
            const int r0 = runStart & ~1, r1 = runEnd | 1;   // the CO5300 wants even windows
            pending = {buf + (r0 - x0) * W, 0, static_cast<int16_t>(r0), static_cast<int16_t>(W),
                       static_cast<int16_t>(r1 - r0 + 1), -1};
            runStart = -1;
        };
        for (int x = x0; x < x1; ++x) {
            const uint32_t h = hashRow(buf + (x - x0) * W, W);
            if (h == rowHash[x] && !sendAll) continue;
            rowHash[x] = h;
            if (runStart >= 0 && x - runEnd > kMergeGap) flush();
            if (runStart < 0) runStart = x;
            runEnd = x;
        }
        flush();
        busy += nowUs() - t;
        if (pending.px) { pending.release = static_cast<int8_t>(k); xQueueSend(jobs, &pending, portMAX_DELAY); }
        else xSemaphoreGive(stripFree[k]);   // nothing changed in this strip
    }
    sendAll = false;
#ifdef POWER_LOG
    if (lit) plog::litDone();
#endif
    queueEnd();
    return busy;
}

// The Stops list at rest, when only its live marks blink (ui::restKey): draws and queues just the marks' windows
// (even-aligned, as the CO5300 wants); the panel already shows the rest of this frame. tools/frame_test.cpp checks
// that this leaves the panel exactly as a whole frame would. Returns the microseconds spent drawing.
uint32_t drawMarks(uint32_t ms, const ui::Mark* mk, int n) {
    uint32_t busy = 0;
#ifdef POWER_LOG
    ++frameSeq;
#endif
    for (int i = 0; i < n; ++i) {
        const int x0 = max(0, mk[i].x & ~1), x1 = min(W, (mk[i].x + mk[i].w + 1) & ~1);
        const int y0 = max(0, mk[i].y & ~1), y1 = min(W, (mk[i].y + mk[i].h + 1) & ~1);
        if (x0 >= x1 || y0 >= y1) continue;
        const int k = i & 1, rows = y1 - y0;
        xSemaphoreTake(stripFree[k], portMAX_DELAY);   // its last send has finished
        const uint32_t t = nowUs();
        ui::Canvas cv = ui::Canvas::panelWindow(stripBuf[k], W, x0, x1, y0, rows);
        ui::draw(cv, app, model, ms, scratch);
        // These panel rows' hashes no longer describe the panel: flip them so the next whole frame sends the rows.
        for (int x = x0; x < x1; ++x) rowHash[x] = ~rowHash[x];
        busy += nowUs() - t;
        const Job j{stripBuf[k], static_cast<int16_t>(W - y0 - rows), static_cast<int16_t>(x0), static_cast<int16_t>(rows),
                    static_cast<int16_t>(x1 - x0), static_cast<int8_t>(k)};
        xQueueSend(jobs, &j, portMAX_DELAY);
    }
    queueEnd();
    return busy;
}

// ---------- touch: sampled on its own task ----------

QueueHandle_t touchEvents;   // touchf::Event

// Reads the touch panel every 8 ms; touchf::Filter turns the reads into presses (and rides out the controller's
// empty reads between reports).
#ifdef HANDHELD_LIVE
// The power chip shares the touch controller's I2C bus, and the drivers read Wire's buffer outside its lock, so
// only this task touches the bus: it reads the AXP2101 once a second and hands the state over under pwLock.
portMUX_TYPE pwLock = portMUX_INITIALIZER_UNLOCKED;
power::State pwShared;
volatile bool offReq = false;   // serial "O": this task turns the board off
// The hidden reset (ecosystem spec 5.4): two fingers held 10 s anywhere. The CST9217 reports at most two points
// (more read as none), so the dock's three fingers can't be seen here. Seen in this task, done by the loop.
constexpr uint32_t kResetHoldMs = 10000;
volatile bool resetReq = false;
#endif

void touchTask(void*) {
    touchf::Filter filter;
    TickType_t wake = xTaskGetTickCount();
    // A press holds full speed (cpu_clock.h) from the read that finds it until it ends, so whatever the loop is
    // drawing at 80 MHz finishes at full speed, and a tap on the dim screen, which wakes it on release at least 40 ms
    // later, is drawn exactly as fast as before.
    bool pressed = false;
#ifdef HANDHELD_LIVE
    uint32_t tick = 0;
#endif
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(8));
#ifdef HANDHELD_LIVE
        if (offReq && !power::off()) { offReq = false; puts("power: no power chip, so no off"); }
        if (++tick % 125 == 0) {
            const power::State p = power::read();
            portENTER_CRITICAL(&pwLock);
            pwShared = p;
            portEXIT_CRITICAL(&pwLock);
        }
#endif
        int16_t x[2], y[2];
        const uint8_t n = cst9217::read(x, y, 2);
#ifdef HANDHELD_LIVE
        // The reset hold: a read of one finger restarts it, as does no point for longer than the gaps between the
        // controller's reports (touchf::Filter's 40 ms).
        static uint32_t twoSince = 0, twoSeen = 0;
        const uint32_t now = nowMs();
        if (n >= 2) {
            if (!twoSince) twoSince = now | 1;
            twoSeen = now;
        } else if (n == 1 || now - twoSeen > 40) {
            twoSince = 0;
        }
        if (twoSince && now - twoSince >= kResetHoldMs) resetReq = true;
#endif
        touchf::Event ev[2];
        const int k = filter.feed(nowMs(), n, static_cast<int16_t>(W - 1 - y[0]), x[0], ev);
        for (int i = 0; i < k; ++i) {
            const touchf::Kind kind = ev[i].kind;
            if (kind == touchf::Kind::Down && !pressed) { pressed = true; cpuclk::hold(); }
            if ((kind == touchf::Kind::Up || kind == touchf::Kind::Cancel) && pressed) { pressed = false; cpuclk::release(); }
            xQueueSend(touchEvents, &ev[i], 0);
        }
    }
}

bool touchLog = false;   // serial "t": print every touch sample and what each release did

#ifdef HANDHELD_LIVE
// ---------- the link (ecosystem spec 4–5): what the app set, kept in NVS ("trackie") ----------

// Pet and settings: what GET /config serves. In PSRAM, as the model: internal RAM is for Wi-Fi and TLS.
cta::ConfigMsg& linkCfg = *new (heap_caps_malloc(sizeof(cta::ConfigMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) cta::ConfigMsg();
cta::InfoMsg info = {};   // what GET /info serves
uint16_t petPalette[pet::kPaletteSize];   // Trackie's colors: the default with the app's two swapped in
lan::Inbox* inbox;        // the loop's copy of the link's writes (PSRAM: it holds a 1 KB body)
bool haveToken = false;   // a household token is saved (info.paired, with a bond)
bool infoDirty = false;   // something the app shows changed: notify `info` now, not at the heartbeat
uint32_t settingsSaveMs = 0;   // Settings' last − / + tap waiting to be saved (0 = none): taps in a row write flash once

void applyPet() {
    memcpy(petPalette, pet::kDefaultPalette565, sizeof(petPalette));
    if (linkCfg.hasPet) pet::applyColors(petPalette, linkCfg.pet.colors);
    snprintf(model.petName, sizeof(model.petName), "%s", linkCfg.hasPet ? linkCfg.pet.name : "");   // Settings' name
    ui::petPalette565 = petPalette;   // drawPet reads it; the next frame redraws Trackie
}

void applySettings() {
    fullBrightness = static_cast<uint8_t>(kFullBrightness * linkCfg.settings.screen / 100);
    model.screenPct = linkCfg.settings.screen;   // Settings' Brightness row
    model.nightStart = linkCfg.settings.nightStart;   // Sleepy and slow polling
    model.nightEnd = linkCfg.settings.nightEnd;
}

// Saves a value under key unless flash already holds it (a blob, as Preferences' putBytes stored it).
void saveBytes(const char* key, const void* v, size_t n) {
    nvs_handle_t h;
    if (nvs_open("trackie", NVS_READWRITE, &h) != ESP_OK) { printf("store: saving %s failed\n", key); return; }
    uint8_t old[64];   // a PetConfig or DeviceSettings
    size_t len = sizeof(old);
    if (n > sizeof(old) || nvs_get_blob(h, key, old, &len) != ESP_OK || len != n || memcmp(old, v, n) != 0)
        if (nvs_set_blob(h, key, v, n) != ESP_OK || nvs_commit(h) != ESP_OK) printf("store: saving %s failed\n", key);
    nvs_close(h);
}

// Forgets the saved networks, the token and the Bluetooth bonds (with a new address, so a phone that still holds the
// old bond sees a new device and pairs afresh), then boots fresh (POST /reset, serial "R", the two-finger hold).
// While secrets.h has WIFI_SSID the board rejoins it after the reboot (the build-time fallback, spec 5.1).
void resetLink(const char* why) {
    printf("link: reset (%s): forgetting Wi-Fi, the token and Bluetooth bonds, rebooting\n", why);
    ble_setup::stop();
    ble_setup::forgetBonds();
    nvs_handle_t h;
    if (nvs_open("trackie", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "wifi");
        nvs_erase_key(h, "token");
        nvs_commit(h);
        nvs_close(h);
    }
    fflush(stdout);
    fsync(fileno(stdout));
    esp_restart();
}

// The household token (POST /token over Bluetooth, or serial "T <32 lowercase hex>"), saved and used at once. Never
// echoed.
void saveToken(const char* t) {
    if (!cta::validToken(t)) { puts("link: a token is 32 lowercase hex characters; not set"); return; }
    nvs_handle_t h;
    const bool opened = nvs_open("trackie", NVS_READWRITE, &h) == ESP_OK;
    if (!opened || nvs_set_str(h, "token", t) != ESP_OK || nvs_commit(h) != ESP_OK) puts("store: saving the token failed");
    if (opened) nvs_close(h);
    lan::setToken(t);
    haveToken = true;
    infoDirty = true;
    puts("link: token set");
}

// The app's writes the link answered already: applied and saved here (flash from this task, not httpd's).
void drainLink() {
    if (!inbox || !lan::take(*inbox)) return;
    if (inbox->nearby)
        puts(live::setNearby(inbox->nearbyJson, inbox->nearbyLen) ? "nearby: ok" : "nearby: rejected");
    if (inbox->config && inbox->cfg.hasPet) {
        linkCfg.pet = inbox->cfg.pet;
        linkCfg.hasPet = true;
        saveBytes("pet", &linkCfg.pet, sizeof(linkCfg.pet));
        applyPet();
    }
    if (inbox->config && inbox->cfg.hasSettings) {
        linkCfg.settings = inbox->cfg.settings;
        linkCfg.hasSettings = true;
        saveBytes("settings", &linkCfg.settings, sizeof(linkCfg.settings));
        settingsSaveMs = 0;   // saved just now, a pending tap's value replaced by the app's
        applySettings();
    }
    if (inbox->config) { lan::setConfig(linkCfg); infoDirty = true; }
    if (inbox->token) {
        saveToken(inbox->tokenHex);
        memset(inbox->tokenHex, 0, sizeof(inbox->tokenHex));
    }
    if (inbox->reset) resetLink("POST /reset");
}

// The `info` characteristic (the plan's Status): the JSON every second for reads, and a notify on a change the app
// shows (battery step, charging, Wi-Fi, docked, wake, settings, token or bond, pin) or every kInfoHeartbeatSec.
void publishInfo(uint32_t ms) {
    static cta::InfoMsg sent = {};
    static uint32_t sentMs = 0;
    static char json[cta::kMaxInfo];
    const size_t n = lan::infoJson(json, sizeof(json));
    if (!n) return;
    const bool wifi = live::wifiConnected();
    const int8_t rssi = wifi ? live::wifiRssi() : 0;
    const bool changed = infoDirty || info.battery != sent.battery || info.charging != sent.charging ||
                         wifi != sent.wifiConnected || abs(rssi - sent.rssi) >= 5 || info.docked != sent.docked ||
                         info.wake != sent.wake || info.paired != sent.paired || info.relaying != sent.relaying ||
                         strcmp(info.pin, sent.pin) != 0;
    const bool notify = changed || !sentMs || ms - sentMs >= cta::kInfoHeartbeatSec * 1000;
    ble_setup::setInfo(json, n, notify);
    if (notify) {
        sent = info;
        sent.wifiConnected = wifi;
        sent.rssi = rssi;
        sentMs = ms;
        infoDirty = false;
    }
}
#endif

void handleTouch() {
    touchf::Event e;
    while (xQueueReceive(touchEvents, &e, 0) == pdTRUE) {
#ifdef POWER_LOG
        plog::touch();
#endif
        if (touchLog) printf("T %c %d %d %u\n", "dmucy"[static_cast<int>(e.kind)], e.x, e.y, static_cast<unsigned>(e.ms));
        switch (e.kind) {
            case touchf::Kind::Down: ui::pointerDown(app, model, e.x, e.y, e.ms); break;
            case touchf::Kind::Move: ui::pointerMove(app, model, e.x, e.y, e.ms); break;
            case touchf::Kind::Up:
                ui::pointerUp(app, model, e.x, e.y, e.ms);
                if (touchLog && app.gesture[0]) printf("G %s\n", app.gesture);
                app.gesture[0] = 0;
                break;
            case touchf::Kind::Cancel: app.drag.down = false; break;
            case touchf::Kind::Cycle: ui::cycleScenario(app, model, e.ms); app.lastInput = e.ms; break;
        }
    }
}

// ---------- the CPU clock ----------

// The loop's hold on full speed (cpu_clock.h). Awake, every frame is drawn and sent at full speed, and so is
// anything that moves; between the frames of a still screen (Home's bob, the live mark's blink: twice a second) it
// lets go once the last frame is on the panel. On the dim always-on screen it lets go once that screen is on the
// panel, and draws its once-a-second change slowly. A touch takes full speed in the touch task the moment the press
// is read, before the loop sees it, so every response to a tap is drawn exactly as fast as before.
bool fastHeld = false;

void fullSpeed(bool on) {
    if (on == fastHeld) return;
    fastHeld = on;
    if (on) cpuclk::hold();
    else cpuclk::release();
}

// ---------- serial test input ----------

uint32_t autoScrollUntil = 0;
bool profileReq = false, statsReq = false;

// "p": times drawing the current screen as strips (each strip, then the whole frame), hashing, and the same frame
// drawn whole into PSRAM the old way. Nothing is sent.
void profile(uint32_t ms) {
    for (int k = 0; k < 2; ++k) xSemaphoreTake(stripFree[k], portMAX_DELAY);
    static uint16_t* whole = static_cast<uint16_t*>(heap_caps_malloc(W * W * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    constexpr int kReps = 3;
    uint32_t strip[kStrips] = {}, hash = 0, full = 0;
    for (uint32_t& c : ui::gfxCycles) c = 0;
    for (int r = 0; r < kReps; ++r) {
        ui::gfxClock = []() -> uint32_t { return esp_cpu_get_cycle_count(); };   // the strips only
        for (int s = 0; s < kStrips; ++s) {
            const int x0 = s * kStripCols, x1 = min(W, x0 + kStripCols);
            uint32_t t = nowUs();
            ui::Canvas cv = ui::Canvas::panelWindow(stripBuf[0], W, x0, x1, 0, W);
            ui::draw(cv, app, model, ms, scratch);
            strip[s] += nowUs() - t;
            t = nowUs();
            volatile uint32_t sink = 0;
            for (int x = x0; x < x1; ++x) sink = sink + hashRow(stripBuf[0] + (x - x0) * W, W);
            hash += nowUs() - t;
        }
        ui::gfxClock = nullptr;
        if (whole) {
            const uint32_t t = nowUs();
            ui::Canvas cv(whole, W, W);
            ui::draw(cv, app, model, ms, scratch);
            full += nowUs() - t;
        }
    }
    uint32_t total = 0;
    printf("profile, us per frame: strips");
    for (int s = 0; s < kStrips; ++s) { printf(" %u", static_cast<unsigned>(strip[s] / kReps)); total += strip[s]; }
    printf(" = %u; hash %u; whole frame in PSRAM %u\n", static_cast<unsigned>(total / kReps),
           static_cast<unsigned>(hash / kReps), static_cast<unsigned>(full / kReps));
    printf("profile, us per frame by primitive:");
    for (int p = 0; p < static_cast<int>(ui::Prim::Count); ++p)
        if (ui::gfxCycles[p] / kReps / 240 >= 50)
            printf(" %s %u", ui::kPrimNames[p], static_cast<unsigned>(ui::gfxCycles[p] / kReps / 240));
    printf("\n");
    sendAll = true;   // the strips were drawn over; resend everything next frame
    for (int k = 0; k < 2; ++k) xSemaphoreGive(stripFree[k]);
}

// "h": a hash of each of four fixed frames (tools/preview.cpp's shots of the same names, at its fixed clock), drawn
// through the strips as every frame is. Two builds that print the same hashes draw the same pixels. Nothing is sent.
void frameHashes() {
    for (int k = 0; k < 2; ++k) xSemaphoreTake(stripFree[k], portMAX_DELAY);
    static void* mem = heap_caps_malloc(sizeof(mock::Model), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    struct Shot { const char* name; mock::Scenario sc; ui::Screen screen; bool dim; int detail; uint32_t after; };
    static const Shot kShots[] = {
        {"home_live", mock::Scenario::Live, ui::Screen::Home, false, -1, 40},
        {"trains", mock::Scenario::Live, ui::Screen::Stops, false, -1, 0},
        {"detail_train", mock::Scenario::Sad, ui::Screen::Detail, false, 0, 0},
        {"home_dim", mock::Scenario::Live, ui::Screen::Home, true, -1, 0},
    };
    constexpr uint32_t kStart = 1794019260;   // preview.cpp's: 2026-11-06 20:41 Chicago
    for (const Shot& s : kShots) {
        if (!mem) break;
        mock::Model& m = *new (mem) mock::Model();
        m.scenario = s.sc;
        mock::update(m, kStart + s.after, kStart);
        static ui::App a;
        a = ui::App();
        ui::init(a, 1000);
        a.view.screen = s.screen;
        a.dim = s.dim;
        if (s.detail >= 0) {
            const mock::Card& c = m.trains[s.detail];
            a.view.station = c.station;
            snprintf(a.view.route, sizeof(a.view.route), "%s", c.rec.route);
            snprintf(a.view.dir, sizeof(a.view.dir), "%s", c.rec.dir);
        }
        uint32_t h = 2166136261u;
        for (int pass = 0; pass < 2; ++pass)   // twice: Home learns its hit areas on the first pass
            for (int st = 0; st < kStrips; ++st) {
                constexpr int kStripRows = 48;   // the frames as the Arduino build drew them, so the hashes still compare
                const int y0 = st * kStripRows, rows = y0 + kStripRows <= W ? kStripRows : W - y0;
                ui::Canvas cv = ui::Canvas::panelBand(stripBuf[0], W, y0, rows);
                ui::draw(cv, a, m, 1000, scratch);
                for (int x = 0; pass && x < W; ++x) h = (h ^ hashRow(stripBuf[0] + x * rows, rows)) * 16777619u;
            }
        printf("hash %s %08x\n", s.name, static_cast<unsigned>(h));
    }
    sendAll = true;   // the strips were drawn over; resend everything next frame
    for (int k = 0; k < 2; ++k) xSemaphoreGive(stripFree[k]);
}

// One whole line, or nothing if the host isn't taking data (a print waits at most 5 ms). Under stdout's lock, so it
// never lands inside another task's printf (which the driver sends a character at a time).
void writeLine(const char* s, size_t n) {
    flockfile(stdout);
    usb_serial_jtag_write_bytes(s, n, pdMS_TO_TICKS(5));
    funlockfile(stdout);
}

// In screen coordinates: "d x y" (down), "m x y" (move), "u x y" (up), "c" (debug cycle), "a" (auto-scroll 6 s),
// "p" (profile), "h" (frame hashes), "s" (frame stats now), "t" (touch log on/off: every sample, and what each
// release did). Live build:
// "N {json}", the `nearby` message (spec 8.3) from tools/send_nearby.py, standing in for the app; "O", the
// board off (against burn-in on a board left plugged in); "R", the link reset (ecosystem spec 5.4); "T <32 hex>",
// the household token, as POST /token over Bluetooth sets it (never echoed); "b", Bluetooth off or on; "P", pair again (device::pairAgain); "W", Wi-Fi off until reboot.
void pollSerial(uint32_t ms) {
    static char line[640];
    static int n = 0;
    char ch;
    while (usb_serial_jtag_read_bytes(&ch, 1, 0) == 1) {
        if (ch != '\n' && n < static_cast<int>(sizeof(line)) - 1) { line[n++] = ch; continue; }
        const int len = n;
        line[n] = 0; n = 0;
#ifdef HANDHELD_LIVE
        if (line[0] == 'N' && line[1] == ' ') {
            puts(live::setNearby(line + 2, len - 2) ? "nearby: ok" : "nearby: rejected");
            continue;
        }
        if (line[0] == 'O') {   // the board off, by whichever task owns the I2C bus
            if (touchOk) offReq = true;
            else if (!power::off()) puts("power: no power chip, so no off");
            continue;
        }
        if (line[0] == 'b' && len <= 2) {   // BLE off or on, for measurements (on from boot)
            if (ble_setup::running()) ble_setup::stop();
            else if (!ble_setup::start(model.pairName)) puts("ble: didn't start");
            printf("ble: %s; internal free %u (min %u)\n", ble_setup::running() ? "on" : "off",
                   static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                   static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
            continue;
        }
        if (line[0] == 'R' && len <= 2) resetLink("serial");   // "R" alone (a CR may trail it)
        if (line[0] == 'P' && len <= 2) { device::pairAgain(); continue; }
        if (line[0] == 'W' && len <= 2) { live::wifiOff(); continue; }   // Wi-Fi off until reboot (no-Wi-Fi demo)
        if (line[0] == 'T' && line[1] == ' ') {
            char* t = line + 2;
            t[strcspn(t, "\r ")] = 0;
            saveToken(t);
            memset(line, 0, sizeof(line));   // the token leaves no copy in the line buffer
            continue;
        }
#else
        (void)len;
#endif
        float x = 0, y = 0;
        sscanf(line + 1, "%f %f", &x, &y);
        if (line[0] == 'd') ui::pointerDown(app, model, x, y, ms);
        else if (line[0] == 'm') ui::pointerMove(app, model, x, y, ms);
        else if (line[0] == 'u') ui::pointerUp(app, model, x, y, ms);
        else if (line[0] == 'c') ui::cycleScenario(app, model, ms);
        else if (line[0] == 'a') autoScrollUntil = ms + 6000;   // scroll the list by itself, for timing
        else if (line[0] == 'p') profileReq = true;
        else if (line[0] == 's') statsReq = true;
        else if (line[0] == 'h') { frameHashes(); continue; }
#ifdef POWER_LOG
        else if (line[0] == 'S') { stress::start(ms); continue; }   // the power analysis stress test
        else if (line[0] == 'X') { stress::stop(ms); continue; }
#endif
        else if (line[0] == 't') { touchLog = !touchLog; printf("touch log %s\n", touchLog ? "on" : "off"); }   // print the frame stats now and start a new window
        app.lastInput = ms;
    }
}

// cJSON's nodes (json_heap.h) in PSRAM, internal only if PSRAM runs out: internal RAM is for Wi-Fi and TLS.
void* jsonAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void jsonFree(void* p) { heap_caps_free(p); }
const cta::JsonHeap kJsonHeap = {jsonAlloc, jsonFree, nullptr};

}  // namespace

#ifdef HANDHELD_LIVE
// The Settings page's hooks (device_hooks.h), replacing mock_data.cpp's weak defaults.
namespace device {

// Settings → Pair again, confirmed (and serial "P"): forgets the Bluetooth bond, the token and the BLE address, and
// goes back to pairing mode without a reboot. Wi-Fi and the app's settings stay.
void pairAgain() {
    puts("link: pairing again: forgetting the bond and the token");
    ble_setup::stop();
    ble_setup::forgetBonds();
    nvs_handle_t h;
    if (nvs_open("trackie", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "token");
        nvs_commit(h);
        nvs_close(h);
    }
    lan::pairAgain();   // the token out of use, and a fresh Wi-Fi code
    haveToken = false;
    infoDirty = true;
    if (!ble_setup::start(model.pairName)) puts("ble: didn't start");   // a new random address (forgetBonds)
}

bool bleConnected() { return ble_setup::connected(); }

bool paired() { return haveToken || ble_setup::bonded(); }

}  // namespace device
#endif

void setup() {
    cta::setJsonHeap(&kJsonHeap);   // before anything is parsed
    // USB serial through its driver, so a print never waits long on a host that isn't reading: plugged into a
    // computer with nothing reading the port, a print blocks at most 50 ms once, then output is dropped until the
    // host reads again (usb_serial_jtag_vfs.c). Whole lines (writeLine, the power log) wait at most 5 ms.
#ifdef POWER_LOG
    usb_serial_jtag_driver_config_t usb = {4096, 1024};   // tx: the power log writes whole lines only, so a line must fit
#else
    usb_serial_jtag_driver_config_t usb = {1024, 1024};   // tx: a burst of lines fits while the host reads
#endif
    // rx 1024: a `nearby` line is about 250 bytes; 256 would drop a tail mid-frame
    usb_serial_jtag_driver_install(&usb);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdout, nullptr, _IOLBF, 0);
    cpuclk::begin();   // before the network task, which holds full speed through each request
    fullSpeed(true);   // awake from boot
    i2c_master_bus_config_t i2cCfg = {};
    i2cCfg.i2c_port = I2C_NUM_0;
    i2cCfg.sda_io_num = static_cast<gpio_num_t>(kSda);
    i2cCfg.scl_io_num = static_cast<gpio_num_t>(kScl);
    i2cCfg.clk_source = I2C_CLK_SRC_DEFAULT;
    i2cCfg.glitch_ignore_cnt = 7;
    i2cCfg.flags.enable_internal_pullup = true;   // as Wire.begin did
    if (i2c_new_master_bus(&i2cCfg, &i2c) != ESP_OK) puts("i2c: bus failed");
    for (int k = 0; k < 2; ++k) {
        stripBuf[k] = static_cast<uint16_t*>(heap_caps_malloc(W * kStripCols * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        stripFree[k] = xSemaphoreCreateBinary();
        xSemaphoreGive(stripFree[k]);
    }
    if (!lcdBegin()) puts("display: begin failed");
    if (stripBuf[0]) {   // black, as Arduino_GFX's fillScreen(0)
        memset(stripBuf[0], 0, kStripBytes);
        for (int x = 0; x < W; x += kStripCols) lcdDraw(x, 0, min(kStripCols, W - x), W, stripBuf[0]);
    }
    gpio_config_t te = {};   // the panel's TE line (onTe)
    te.pin_bit_mask = 1ULL << kLcdTe;
    te.mode = GPIO_MODE_INPUT;
    te.intr_type = GPIO_INTR_ANYEDGE;
    if (gpio_config(&te) != ESP_OK || gpio_install_isr_service(ESP_INTR_FLAG_IRAM) != ESP_OK ||
        gpio_isr_handler_add(static_cast<gpio_num_t>(kLcdTe), onTe, nullptr) != ESP_OK)
        puts("display: no TE, frames go out unpaced");
#ifdef HANDHELD_LIVE
    // PSRAM in the live build: Wi-Fi and a TLS connection need about 100 KB of internal RAM, and these 69 KB don't
    // draw measurably faster inside it (see the README's speed notes).
    scratch = static_cast<uint16_t*>(heap_caps_malloc(ui::kScratchPixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    scratch = static_cast<uint16_t*>(heap_caps_malloc(ui::kScratchPixels * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
    if (!stripBuf[0] || !stripBuf[1] || !scratch) puts("memory: allocation failed");
    // 32 rendered cards (2.2 MB of PSRAM): both tabs' cards, at rest and moving, fit at once, so switching tabs
    // or opening a card doesn't re-render the other tab's cards (16 didn't fit 12 trains and 5 buses).
    constexpr int kCards = 32;
    if (void* cache = heap_caps_malloc(ui::cardCacheBytes(kCards), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
        ui::setCardCache(cache, kCards);
    touchOk = i2c && cst9217::begin(i2c, kTpRst, kTpInt);
    printf("reset reason %d (1 power-on, 3 software, 4 panic, 6 task watchdog, 9 brownout)\n",
           static_cast<int>(esp_reset_reason()));
    printf("touch: %s, cpu %u MHz, internal free %u, psram free %u\n", touchOk ? "ok" : "failed",
           static_cast<unsigned>(cpuclk::mhz()), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    printf("sdk: esp-idf %s, code runs from %s, data cache line %d B\n", esp_get_idf_version(),
#ifdef CONFIG_SPIRAM_XIP_FROM_PSRAM
                  "PSRAM (XIP)",
#else
                  "flash",
#endif
                  CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE);
    startUnix = buildUnix();
    startMs = nowMs();
#ifdef HANDHELD_LIVE
    snap = static_cast<live::Snapshot*>(heap_caps_malloc(sizeof(live::Snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    live::clear(*snap);
    printf("power: %s\n", i2c && power::begin(i2c) ? "AXP2101 ok" : "AXP2101 not found");
    pw = pwShared = power::read();   // before the touch task starts; after that only it reads the bus
    mock::fromLive(model, *snap, pw, startUnix, false);
    {   // the link: the token, and what the app set last (pet, settings)
        char tok[cta::kTokenLen + 1] = "";
        linkCfg.settings = {100, 100, static_cast<uint16_t>(cta::kNightStartMin), static_cast<uint16_t>(cta::kNightEndMin)};
        nvs_handle_t h;
        if (nvs_open("trackie", NVS_READONLY, &h) == ESP_OK) {   // Preferences' entries: a string and two blobs
            size_t n = sizeof(tok);
            if (nvs_get_str(h, "token", tok, &n) != ESP_OK) tok[0] = 0;
            haveToken = tok[0] != 0;
            n = sizeof(linkCfg.pet);
            linkCfg.hasPet = nvs_get_blob(h, "pet", &linkCfg.pet, &n) == ESP_OK && n == sizeof(linkCfg.pet);
            n = sizeof(linkCfg.settings);
            cta::DeviceSettings st;
            linkCfg.hasSettings = nvs_get_blob(h, "settings", &st, &n) == ESP_OK && n == sizeof(st);
            if (linkCfg.hasSettings) linkCfg.settings = st;
            nvs_close(h);
        }
        linkCfg.pet.name[cta::kPetNameLen] = 0;
        inbox = new (heap_caps_malloc(sizeof(lan::Inbox), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) lan::Inbox();
        lan::begin(tok);   // before live::start: the network task starts the link once Wi-Fi is up
        applyPet();
        applySettings();
        lan::setConfig(linkCfg);
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(info.id, sizeof(info.id), "%02x%02x", mac[4], mac[5]);
        snprintf(info.fw, sizeof(info.fw), "%s", __DATE__);
        snprintf(model.pairName, sizeof(model.pairName), "Trackie-%s", info.id);
        if (!ble_setup::start(model.pairName)) puts("ble: didn't start");   // always on (the plan's default 7)
    }
    live::start();
#ifdef POWER_LOG
    plog::begin();
#endif
#else
    mock::update(model, startUnix, startUnix);
#endif
    ui::init(app, nowMs());
#ifdef HANDHELD_LIVE
    if (!device::paired()) app.view.screen = ui::Screen::Pair;   // pairing mode until paired (Leo, 2026-11-14)
#endif
    jobs = xQueueCreate(32, sizeof(Job));
    touchEvents = xQueueCreate(64, sizeof(touchf::Event));
    // Core 0 sends (the bus driver busy-waits) and samples touch; core 1 (this loop) draws. The sender can keep
    // core 0 busy for long stretches while the list moves, so the watchdog stops checking the idle tasks.
    const esp_task_wdt_config_t wdt = {.timeout_ms = 5000, .idle_core_mask = 0, .trigger_panic = false};
    esp_task_wdt_reconfigure(&wdt);
    xTaskCreatePinnedToCore(sendTask, "send", 4096, nullptr, 2, nullptr, 0);
    if (touchOk) xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 3, nullptr, 0);
}

bool inMotion = false;   // the last frame drawn was part of an animation

void loop() {
    static uint32_t lastData = 0, lastFrame = 0, frames = 0, statT = 0, sumDraw = 0, sumFrame = 0;
    // In motion, each frame starts just before a scan does, so frames show at a steady one or two scans each (a frame
    // drawn mid-scan can miss its scan, and the next can't, which judders); before the clock is read, so motion matches.
    if (inMotion && tePeriod && nowUs() - teFall < 4 * tePeriod) waitUntil(teFall + tePeriod - kLeadUs);
    if (uxQueueMessagesWaiting(touchEvents)) fullSpeed(true);   // a touch: full speed before anything answers it
    handleTouch();
    const uint32_t ms = nowMs();   // after the touch events, so none is stamped later than this frame
    pollSerial(ms);
    if (ms - lastData >= 1000 || lastData == 0) {
        // New data changes what the next frame shows, so it's taken in at full speed, as the frame is drawn (except
        // on the dim always-on screen, as before).
        if (!ui::alwaysOn(app)) fullSpeed(true);
#ifdef HANDHELD_LIVE
        // The clock: NTP once synced, the build time until then.
        const uint32_t ntp = live::unixNow();
        const uint32_t now = ntp ? ntp : startUnix + (ms - startMs) / 1000;
        if (!touchOk) pwShared = power::read();   // no touch task, so nothing else is on the bus
        portENTER_CRITICAL(&pwLock);
        pw = pwShared;
        portEXIT_CRITICAL(&pwLock);
        live::read(*snap, snapVersion);
        mock::fromLive(model, *snap, pw, now, ntp != 0);
        const int lm = mock::localMinutes(now);
        bool night = model.night(lm);   // the app's night hours
#ifdef POWER_LOG
        if (stress::running()) night = false;   // stress-v2: daytime polling during a run, whatever the hour
#endif
        live::setSlow(model.docked || night);
        // Docking clears the pin (spec 5.6). GET /info's view of the wearable, once a second.
        static bool wasDocked = false;
        if (model.docked && !wasDocked) live::setPin(nullptr);
        wasDocked = model.docked;
        info.battery = static_cast<int8_t>(pw.battery ? pw.percent : -1);
        info.charging = pw.charging;
        info.lastUpdate = model.fetchedAt;
        info.docked = model.docked;
        info.paired = device::paired();
        info.relaying = live::relaying();
        snprintf(info.pin, sizeof(info.pin), "%s", snap->hasPin ? snap->pin.rec.id : "");   // spec 5.6: the app sees a cleared pin
        info.hasSettings = true;
        info.settings = linkCfg.settings;
        lan::setInfo(info);
        publishInfo(ms);
        // Unpaired with Wi-Fi: the code for pairing over the network (W6d), or its cooldown.
        char code[cta::kPairCodeLen + 1];
        uint8_t waitS;
        const lan::PairShow show = lan::pairShow(code, waitS);
        model.pairCode = show == lan::PairShow::Code ? static_cast<uint32_t>(atoi(code)) : 0;
        model.pairWaitS = show == lan::PairShow::Wait ? waitS : 0;
        // The battery log (spec 8.5): every 15 min, and once at boot.
        static uint32_t lastLog = 0;
        if (lastLog == 0 || ms - lastLog >= 15 * 60 * 1000) {
            lastLog = ms;
            printf("power: usb %d, battery %s %d%% %u mV%s; docked %d\n", pw.usb, pw.battery ? "yes" : "no", pw.percent,
                   pw.battMv, pw.charging ? ", charging" : "", model.docked);
        }
#else
        mock::update(model, startUnix + (ms - startMs) / 1000, startUnix);
#endif
        lastData = ms;
    }
    if (autoScrollUntil) {
        const int t = static_cast<int>(app.view.tab);
        if (ms < autoScrollUntil) {
            app.scrollTarget[t] = 560 + 400 * sinf(ms / 700.f);
            app.lastInput = ms;
        } else {   // come to rest on a card, as a release does
            app.scrollTarget[t] = ui::kRow * roundf(app.scrollTarget[t] / ui::kRow);
            autoScrollUntil = 0;
        }
    }
#ifdef POWER_LOG
    stress::tick(app, model, ms, snap->loaded);
#endif
#ifdef HANDHELD_LIVE
    // The Bluetooth passkey (spec 10), read every pass so it's on screen the moment iOS asks: the Pair screen, kept
    // lit. The Pair screen goes Home once paired and no passkey shows (ui::tick).
    model.passkey = ble_setup::passkey();
    if (model.passkey) {
        if (app.view.screen != ui::Screen::Pair) { app.view.screen = ui::Screen::Pair; app.trans.on = false; }
        app.dim = false;
        app.lastInput = ms;
    }
#endif
    ui::tick(app, model, ms);
#ifdef HANDHELD_LIVE
    static bool wasDim = false;   // a tap on the dim screen is a wake (spec 8.2)
    if (wasDim && !app.dim) {
        live::wake();
        ++info.wake;   // the app sees it in GET /info and sends a fresh nearby list (ecosystem spec 5.5)
        lan::setInfo(info);
    }
    wasDim = app.dim;
    drainLink();
    if (resetReq) resetLink("two-finger hold");
#endif
    if (model.screenChanged) {   // Settings' − / + (ecosystem spec 5.2): applied at once
        model.screenChanged = false;
#ifdef HANDHELD_LIVE
        linkCfg.settings.screen = model.screenPct;
        linkCfg.hasSettings = true;
        applySettings();
        lan::setConfig(linkCfg);
        infoDirty = true;   // info.settings goes out with the next second's notify, so the app's page follows
        settingsSaveMs = ms ? ms : 1;
#else
        fullBrightness = static_cast<uint8_t>(kFullBrightness * model.screenPct / 100);
#endif
    }
#ifdef HANDHELD_LIVE
    if (settingsSaveMs && ms - settingsSaveMs >= 2000) {
        settingsSaveMs = 0;
        saveBytes("settings", &linkCfg.settings, sizeof(linkCfg.settings));
    }
#endif
    // Frames: every frame while something moves; otherwise only when Trackie bobs or the live mark blinks (every
    // 500 ms), and a few frames a second while dim (spec 8: the animation drops when dimmed). A tap that wakes the
    // dim screen is drawn at once, not at the next half second. Between frames, wait on touch.
    static uint32_t lastHalf = UINT32_MAX;
    static bool wasMoving = false, shownDim = false;
    const bool moving = !app.dim && ui::animating(app, model, ms);
    const bool settling = wasMoving && !moving;   // one smooth frame right after motion stops
    wasMoving = moving;
    const bool woke = shownDim && !app.dim;
    const bool idle = !woke && ((!moving && !settling && ms / 500 == lastHalf) || (app.dim && ms - lastFrame < 250));
    static bool aodDrawn = false;   // the last frame drawn was the always-on screen
    static ui::AodKey aodShown;     // and what it showed
    const bool aod = ui::alwaysOn(app);
    // Every input of this frame is in. Full speed while a frame is due or still being sent, while anything moves, and
    // with a finger down; slow between the frames of a still screen. The dim always-on screen runs slow once it's on
    // the panel; the frame that dims to it is drawn at full speed, so the full-brightness moment before the dimmed
    // brightness applies (at the end of the frame) lasts as long as before. The profile runs at full speed so its
    // times compare with the README's (and its cycle counts convert at 240 MHz).
    bool fast = app.drag.down || autoScrollUntil != 0 || profileReq;
    if (aod) fast = fast || !aodDrawn;
    else fast = fast || moving || !idle || framesSent != framesQueued;
    fullSpeed(fast);
    if (profileReq) { profileReq = false; profile(ms); }
#ifdef POWER_LOG
    if (plog::due(ms)) {
        plog::Inputs in;
        in.phase = stress::phase();
        in.dim = app.dim;
        in.brightness = wantBrightness;
        in.usbMv = pw.vbusMv;
        in.sysMv = pw.sysMv;
        in.pmuTempC = pw.tempC;
        in.battery = pw.battery;
        in.battPct = pw.percent;
        in.battMv = pw.battMv;
        in.trainCards = model.nTrains;
        in.busCards = model.nBuses;
        uint32_t oldest = 0;
        for (int i = 0; i < snap->nTrains; ++i)
            if (snap->trains[i].rec.ts && (!oldest || snap->trains[i].rec.ts < oldest)) oldest = snap->trains[i].rec.ts;
        for (int i = 0; i < snap->nBuses; ++i)
            if (snap->buses[i].rec.ts && (!oldest || snap->buses[i].rec.ts < oldest)) oldest = snap->buses[i].rec.ts;
        const uint32_t now = live::unixNow();
        in.dataAgeSec = oldest && now >= oldest ? static_cast<int>(now - oldest) : -1;
        plog::sample(ms, in);
    }
    plog::flush();
#endif
    // The dim always-on screen changes once a second at most (Trackie's bob), plus the clock and the burn-in shift: a
    // frame that would draw it exactly as it is on the panel is skipped (ui::AodKey covers everything it shows).
    bool skip = false;
    if (!idle && aod && aodDrawn && !sendAll) skip = ui::aodKey(app, model, ms) == aodShown;
    if (idle || skip) {
        if (skip) { lastHalf = ms / 500; lastFrame = ms; }
        touchf::Event e;
        xQueuePeek(touchEvents, &e, pdMS_TO_TICKS(4));
        return;
    }
    lastHalf = ms / 500;
    lastFrame = ms;
    const uint32_t t0 = nowUs();
    // The list at rest whose last frame had the same key: only its live marks change.
    static uint32_t restShown = 0;
    ui::Mark marks[8];
    int nMarks = 0;
    const uint32_t rest = ui::restKey(app, model, ms, marks, 8, nMarks);
#ifdef POWER_LOG
    const bool whole = plog::litWanted();   // the lit fraction is measured over a whole frame
#else
    const bool whole = false;
#endif
    if (rest && rest == restShown && !sendAll && !whole) sumDraw += drawMarks(ms, marks, nMarks);
    else sumDraw += drawFrame(ms);
    restShown = rest;
    sumFrame += nowUs() - t0;
    aodDrawn = aod;
    if (aod) aodShown = ui::aodKey(app, model, ms);
    shownDim = app.dim;
    inMotion = moving;
    ++frames;
#ifdef POWER_LOG
    plog::frame(moving, ms);
#endif
    if (ms - statT > 10000 || statsReq) {
        statsReq = false;
        const uint32_t f = frames ? frames : 1;
        // One write, so the line arrives whole or not at all while the host reads (prints wait at most 5 ms; see
        // setup()).
        static char line[512];
        int k = snprintf(line, sizeof(line), "fps %.1f; per frame: draw %u us, loop %u us, send %u us, %u px (of %u); "
                         "scan %u us (TE high %u), late sends %u; ",
                         frames * 1000.f / (ms - statT), sumDraw / f, sumFrame / f, sendUs / f, sentPx / f, W * W,
                         tePeriod, teHigh, lateJobs);
        k += snprintf(line + k, sizeof(line) - k, "scenario %s, mood %d, %d trains, %d buses, internal free %u (min %u)",
                      mock::scenarioName(model.scenario), static_cast<int>(model.mood), model.nTrains, model.nBuses,
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
#ifdef HANDHELD_LIVE
        k += snprintf(line + k, sizeof(line) - k,
                      ", ble %d%s, wifi %d, CTA requests %u, httpd stack left %u, ble stack left %u%s%s",
                      ble_setup::running(), ble_setup::connected() ? " paired" : "", snap->wifiUp,
                      static_cast<unsigned>(snap->requests), static_cast<unsigned>(lan::stackLeft()),
                      static_cast<unsigned>(ble_setup::stackLeft()),
                      snap->status[0] ? ", " : "", snap->status);
        if (snap->hasPin) k += snprintf(line + k, sizeof(line) - k, ", pinned %s %s", snap->pin.rec.id, snap->pin.rec.route);
        static const char* const kFollowStates[] = {"waiting", "due", "arrived", "lost"};   // cta::FollowState
        if (snap->hasFollow)   // the follow's countdown (spec 5.7), for checking on the board
            k += snprintf(line + k, sizeof(line) - k, ", follow %s %d min",
                          kFollowStates[static_cast<int>(snap->follow.state)], snap->follow.min);
#endif
        k += snprintf(line + k, sizeof(line) - k, "\r\n");
        writeLine(line, std::min<size_t>(k, sizeof(line) - 1));
        frames = sumDraw = sumFrame = 0;
        sentPx = 0;
        sendUs = 0;
        lateJobs = 0;
        statT = ms;
    }
}

extern "C" void app_main() {
    // Arduino's start-up did this: a full or outdated NVS partition is erased and started over; any other error is
    // left alone, since erasing would lose the token and the saved lists.
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    setup();
    for (;;) loop();   // on core 1, with an 8 KB stack (sdkconfig.defaults), as Arduino's loop task
}
