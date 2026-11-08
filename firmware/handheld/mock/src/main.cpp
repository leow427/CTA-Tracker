// The handheld mock on the device: display (CO5300 over QSPI), touch (CST9217), and the frame loop.
//
// Frames are drawn in strips of screen rows into internal RAM (fast, unlike PSRAM), already laid out the way the
// panel wants them: the panel is mounted a quarter turn off, so a strip of screen rows is a strip of panel columns,
// rotated 90 degrees right and byte-swapped. Each panel row of a strip is hashed, and only the rows that changed
// since the last frame are sent, from a task on core 0 while core 1 draws the next strip. Touch is sampled on its
// own task every 8 ms, so gestures get real timings; the touch panel reads a half turn from the display, so a
// touch at (tx, ty) is screen point (W-1-ty, tx).
#include <Arduino.h>
#include <Wire.h>

#include "Arduino_GFX_Library.h"
#include "esp_task_wdt.h"
#include "TouchDrvCSTXXX.hpp"
#include "mock_data.h"
#include "touch_filter.h"
#include "ui.h"

namespace {

// Waveshare ESP32-S3-Touch-AMOLED-1.75 pins (from Waveshare's pin_config.h).
constexpr int kLcdCs = 12, kLcdSclk = 38, kLcdD0 = 4, kLcdD1 = 5, kLcdD2 = 6, kLcdD3 = 7, kLcdRst = 39;
constexpr int kSda = 15, kScl = 14, kTpInt = 11, kTpRst = 40;
constexpr int W = ui::W;
constexpr uint8_t kFullBrightness = 220;

Arduino_DataBus* bus = new Arduino_ESP32QSPI(kLcdCs, kLcdSclk, kLcdD0, kLcdD1, kLcdD2, kLcdD3);
Arduino_CO5300* gfx = new Arduino_CO5300(bus, kLcdRst, 0, W, W, 6, 0, 0, 0);
TouchDrvCST92xx touch;
bool touchOk = false;

uint16_t* scratch;
mock::Model model;
ui::App app;
uint32_t startUnix = 0, startMs = 0;

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

constexpr int kStripRows = 48;                                     // screen rows per strip
constexpr int kStrips = (W + kStripRows - 1) / kStripRows;         // 10; the last is 34 rows
constexpr int kMergeGap = 8;                                       // unchanged rows worth sending to save a window

uint16_t* stripBuf[2];            // DMA-capable internal RAM; the bus sends straight from it
SemaphoreHandle_t stripFree[2];
uint32_t rowHash[kStrips][W];     // per strip, per panel row (= screen column): what the panel shows
bool sendAll = true;

struct Job {
    const uint16_t* px;   // nullptr: end of frame (apply the brightness)
    int16_t x, y, w, h;   // panel window
    int8_t release;       // the strip buffer this job finishes with, or -1
};
QueueHandle_t jobs;
volatile uint8_t wantBrightness = kFullBrightness;
volatile uint32_t sentPx = 0, sendUs = 0;

void sendTask(void*) {
    uint8_t brightnessNow = 0;
    Job j;
    for (;;) {
        xQueueReceive(jobs, &j, portMAX_DELAY);
        if (j.px) {
            const uint32_t t = micros();
            gfx->draw16bitBeRGBBitmap(j.x, j.y, const_cast<uint16_t*>(j.px), j.w, j.h);
            sendUs += micros() - t;
            sentPx += j.w * j.h;
        } else if (wantBrightness != brightnessNow) {
            brightnessNow = wantBrightness;
            gfx->setBrightness(brightnessNow);
        }
        if (j.release >= 0) xSemaphoreGive(stripFree[j.release]);
    }
}

inline uint32_t hashRow(const uint16_t* p, int n) {   // FNV-1a over 32-bit words; n is even
    const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
    uint32_t h = 2166136261u;
    for (int i = 0; i < n / 2; ++i) h = (h ^ w[i]) * 16777619u;
    return h;
}

// Draws the frame strip by strip and queues the changed rows. Returns the microseconds spent drawing.
uint32_t drawFrame(uint32_t ms) {
    uint32_t busy = 0;
    for (int s = 0; s < kStrips; ++s) {
        const int k = s & 1, y0 = s * kStripRows, rows = y0 + kStripRows <= W ? kStripRows : W - y0;
        xSemaphoreTake(stripFree[k], portMAX_DELAY);   // its last send has finished
        const uint32_t t = micros();
        uint16_t* buf = stripBuf[k];
        ui::Canvas cv = ui::Canvas::panelBand(buf, W, y0, rows);
        ui::draw(cv, app, model, ms, scratch);
        // Panel window: columns [W - y0 - rows, W - y0), and the panel rows (screen columns) that changed, in runs.
        const int px = W - y0 - rows;
        int runStart = -1, runEnd = -1;
        Job pending{nullptr, 0, 0, 0, 0, -1};
        auto flush = [&]() {
            if (runStart < 0) return;
            if (pending.px) xQueueSend(jobs, &pending, portMAX_DELAY);
            const int r0 = runStart & ~1, r1 = runEnd | 1;   // the CO5300 wants even windows
            pending = {buf + r0 * rows, static_cast<int16_t>(px), static_cast<int16_t>(r0), static_cast<int16_t>(rows),
                       static_cast<int16_t>(r1 - r0 + 1), -1};
            runStart = -1;
        };
        for (int x = 0; x < W; ++x) {
            const uint32_t h = hashRow(buf + x * rows, rows);
            if (h == rowHash[s][x] && !sendAll) continue;
            rowHash[s][x] = h;
            if (runStart >= 0 && x - runEnd > kMergeGap) flush();
            if (runStart < 0) runStart = x;
            runEnd = x;
        }
        flush();
        busy += micros() - t;
        if (pending.px) { pending.release = static_cast<int8_t>(k); xQueueSend(jobs, &pending, portMAX_DELAY); }
        else xSemaphoreGive(stripFree[k]);   // nothing changed in this strip
    }
    sendAll = false;
    wantBrightness = static_cast<uint8_t>(kFullBrightness * ui::brightness(app, model));
    const Job end{nullptr, 0, 0, 0, 0, -1};
    xQueueSend(jobs, &end, portMAX_DELAY);
    return busy;
}

// ---------- touch: sampled on its own task ----------

QueueHandle_t touchEvents;   // touchf::Event

// Reads the touch panel every 8 ms; touchf::Filter turns the reads into presses (and rides out the controller's
// empty reads between reports).
void touchTask(void*) {
    touchf::Filter filter;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(8));
        int16_t x[2], y[2];
        const uint8_t n = touch.getPoint(x, y, 2);
        touchf::Event ev[2];
        const int k = filter.feed(millis(), n, static_cast<int16_t>(W - 1 - y[0]), x[0], ev);
        for (int i = 0; i < k; ++i) xQueueSend(touchEvents, &ev[i], 0);
    }
}

bool touchLog = false;   // serial "t": print every touch sample and what each release did

void handleTouch() {
    touchf::Event e;
    while (xQueueReceive(touchEvents, &e, 0) == pdTRUE) {
        if (touchLog) Serial.printf("T %c %d %d %u\n", "dmucy"[static_cast<int>(e.kind)], e.x, e.y, static_cast<unsigned>(e.ms));
        switch (e.kind) {
            case touchf::Kind::Down: ui::pointerDown(app, model, e.x, e.y, e.ms); break;
            case touchf::Kind::Move: ui::pointerMove(app, model, e.x, e.y, e.ms); break;
            case touchf::Kind::Up:
                ui::pointerUp(app, model, e.x, e.y, e.ms);
                if (touchLog && app.gesture[0]) Serial.printf("G %s\n", app.gesture);
                app.gesture[0] = 0;
                break;
            case touchf::Kind::Cancel: app.drag.down = false; break;
            case touchf::Kind::Cycle: ui::cycleScenario(app, model, e.ms); app.lastInput = e.ms; break;
        }
    }
}

// ---------- serial test input ----------

uint32_t autoScrollUntil = 0;
bool profileReq = false, statsReq = false;

// "p": times drawing the current screen as strips (each strip, then the whole frame), hashing, and the same frame
// drawn whole into PSRAM the old way. Nothing is sent.
void profile(uint32_t ms) {
    for (int k = 0; k < 2; ++k) xSemaphoreTake(stripFree[k], portMAX_DELAY);
    static uint16_t* whole = static_cast<uint16_t*>(ps_malloc(W * W * 2));
    constexpr int kReps = 3;
    uint32_t strip[kStrips] = {}, hash = 0, full = 0;
    for (uint32_t& c : ui::gfxCycles) c = 0;
    for (int r = 0; r < kReps; ++r) {
        ui::gfxClock = []() -> uint32_t { return ESP.getCycleCount(); };   // the strips only
        for (int s = 0; s < kStrips; ++s) {
            const int y0 = s * kStripRows, rows = y0 + kStripRows <= W ? kStripRows : W - y0;
            uint32_t t = micros();
            ui::Canvas cv = ui::Canvas::panelBand(stripBuf[0], W, y0, rows);
            ui::draw(cv, app, model, ms, scratch);
            strip[s] += micros() - t;
            t = micros();
            volatile uint32_t sink = 0;
            for (int x = 0; x < W; ++x) sink = sink + hashRow(stripBuf[0] + x * rows, rows);
            hash += micros() - t;
        }
        ui::gfxClock = nullptr;
        if (whole) {
            const uint32_t t = micros();
            ui::Canvas cv(whole, W, W);
            ui::draw(cv, app, model, ms, scratch);
            full += micros() - t;
        }
    }
    uint32_t total = 0;
    Serial.print("profile, us per frame: strips");
    for (int s = 0; s < kStrips; ++s) { Serial.printf(" %u", strip[s] / kReps); total += strip[s]; }
    Serial.printf(" = %u; hash %u; whole frame in PSRAM %u\n", total / kReps, hash / kReps, full / kReps);
    Serial.print("profile, us per frame by primitive:");
    for (int p = 0; p < static_cast<int>(ui::Prim::Count); ++p)
        if (ui::gfxCycles[p] / kReps / 240 >= 50) Serial.printf(" %s %u", ui::kPrimNames[p], ui::gfxCycles[p] / kReps / 240);
    Serial.println();
    sendAll = true;   // the strips were drawn over; resend everything next frame
    for (int k = 0; k < 2; ++k) xSemaphoreGive(stripFree[k]);
}

// In screen coordinates: "d x y" (down), "m x y" (move), "u x y" (up), "c" (debug cycle), "a" (auto-scroll 6 s),
// "p" (profile), "s" (frame stats now), "t" (touch log on/off: every sample, and what each release did).
void pollSerial(uint32_t ms) {
    static char line[32];
    static int n = 0;
    while (Serial.available()) {
        const char ch = static_cast<char>(Serial.read());
        if (ch != '\n' && n < 31) { line[n++] = ch; continue; }
        line[n] = 0; n = 0;
        float x = 0, y = 0;
        sscanf(line + 1, "%f %f", &x, &y);
        if (line[0] == 'd') ui::pointerDown(app, model, x, y, ms);
        else if (line[0] == 'm') ui::pointerMove(app, model, x, y, ms);
        else if (line[0] == 'u') ui::pointerUp(app, model, x, y, ms);
        else if (line[0] == 'c') ui::cycleScenario(app, model, ms);
        else if (line[0] == 'a') autoScrollUntil = ms + 6000;   // scroll the list by itself, for timing
        else if (line[0] == 'p') profileReq = true;
        else if (line[0] == 's') statsReq = true;
        else if (line[0] == 't') { touchLog = !touchLog; Serial.printf("touch log %s\n", touchLog ? "on" : "off"); }   // print the frame stats now and start a new window
        app.lastInput = ms;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Wire.begin(kSda, kScl);
    if (!gfx->begin(80000000)) Serial.println("display: begin failed");
    gfx->fillScreen(0);
    for (int k = 0; k < 2; ++k) {
        stripBuf[k] = static_cast<uint16_t*>(heap_caps_malloc(W * kStripRows * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        stripFree[k] = xSemaphoreCreateBinary();
        xSemaphoreGive(stripFree[k]);
    }
    scratch = static_cast<uint16_t*>(heap_caps_malloc(ui::kScratchPixels * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!stripBuf[0] || !stripBuf[1] || !scratch) Serial.println("memory: allocation failed");
    constexpr int kCards = 16;
    if (uint16_t* cache = static_cast<uint16_t*>(ps_malloc(kCards * ui::kScratchPixels * 2))) ui::setCardCache(cache, kCards);
    touch.setPins(kTpRst, kTpInt);
    touchOk = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, kSda, kScl);
    Serial.printf("touch: %s, cpu %u MHz, internal free %u, psram free %u\n", touchOk ? "ok" : "failed",
                  getCpuFrequencyMhz(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
    startUnix = buildUnix();
    startMs = millis();
    mock::update(model, startUnix, startUnix);
    ui::init(app, millis());
    jobs = xQueueCreate(32, sizeof(Job));
    touchEvents = xQueueCreate(64, sizeof(touchf::Event));
    // Core 0 sends (the bus driver busy-waits) and samples touch; core 1 (this loop) draws. The sender can keep
    // core 0 busy for long stretches while the list moves, so the watchdog stops checking the idle tasks.
    const esp_task_wdt_config_t wdt = {.timeout_ms = 5000, .idle_core_mask = 0, .trigger_panic = false};
    esp_task_wdt_reconfigure(&wdt);
    xTaskCreatePinnedToCore(sendTask, "send", 4096, nullptr, 2, nullptr, 0);
    if (touchOk) xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 3, nullptr, 0);
}

void loop() {
    static uint32_t lastData = 0, lastFrame = 0, frames = 0, statT = 0, sumDraw = 0, sumFrame = 0;
    handleTouch();
    const uint32_t ms = millis();   // after the touch events, so none is stamped later than this frame
    pollSerial(ms);
    if (ms - lastData >= 1000 || lastData == 0) {
        mock::update(model, startUnix + (ms - startMs) / 1000, startUnix);
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
    ui::tick(app, model, ms);
    if (profileReq) { profileReq = false; profile(ms); }
    // Full speed while something moves; otherwise only when Trackie bobs or the live mark blinks (every 500 ms),
    // and a few frames a second while dim (spec 8: the animation drops when dimmed). Between frames, wait on touch.
    static uint32_t lastHalf = UINT32_MAX;
    static bool wasMoving = false;
    const bool moving = !app.dim && ui::animating(app, model, ms);
    const bool settling = wasMoving && !moving;   // one smooth frame right after motion stops
    wasMoving = moving;
    const bool idle = (!moving && !settling && ms / 500 == lastHalf) || (app.dim && ms - lastFrame < 250);
    if (idle) {
        touchf::Event e;
        xQueuePeek(touchEvents, &e, pdMS_TO_TICKS(4));
        return;
    }
    lastHalf = ms / 500;
    lastFrame = ms;
    const uint32_t t0 = micros();
    sumDraw += drawFrame(ms);
    sumFrame += micros() - t0;
    ++frames;
    if (ms - statT > 10000 || statsReq) {
        statsReq = false;
        const uint32_t f = frames ? frames : 1;
        Serial.printf("fps %.1f; per frame: draw %u us, loop %u us, send %u us, %u px (of %u); ",
                      frames * 1000.f / (ms - statT), sumDraw / f, sumFrame / f, sendUs / f, sentPx / f, W * W);
        Serial.printf("scenario %s, internal free %u\n", mock::scenarioName(model.scenario),
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        frames = sumDraw = sumFrame = 0;
        sentPx = 0;
        sendUs = 0;
        statT = ms;
    }
}
