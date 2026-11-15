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
#include <Arduino.h>
#include <Wire.h>

#include "Arduino_GFX_Library.h"
#include "esp_task_wdt.h"
#include "TouchDrvCSTXXX.hpp"
#include "cpu_clock.h"
#include "mock_data.h"
#include "row_hash.h"
#include "touch_filter.h"
#include "ui.h"
#ifdef HANDHELD_LIVE
#include "live.h"
#include "live_model.h"
#include "power.h"
#include "cta_config.h"
#endif
#ifdef POWER_LOG
#include "power_log.h"
#include "stress.h"
#endif

namespace {

// Waveshare ESP32-S3-Touch-AMOLED-1.75 pins (from Waveshare's pin_config.h).
constexpr int kLcdCs = 12, kLcdSclk = 38, kLcdD0 = 4, kLcdD1 = 5, kLcdD2 = 6, kLcdD3 = 7, kLcdRst = 39;
constexpr int kLcdTe = 13;   // the schematic's LCD_TE; Waveshare's own examples leave it unused
constexpr int kSda = 15, kScl = 14, kTpInt = 11, kTpRst = 40;
constexpr int W = ui::W;
constexpr uint8_t kFullBrightness = 220;

Arduino_DataBus* bus = new Arduino_ESP32QSPI(kLcdCs, kLcdSclk, kLcdD0, kLcdD1, kLcdD2, kLcdD3);
Arduino_CO5300* gfx = new Arduino_CO5300(bus, kLcdRst, 0, W, W, 6, 0, 0, 0);
TouchDrvCST92xx touch;
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

void IRAM_ATTR onTe() {
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
    for (int32_t d; (d = static_cast<int32_t>(t - micros())) > 0;) {
        if (d > 2000) vTaskDelay(pdMS_TO_TICKS(d / 1000 - 1));   // sleep, leaving the core to other tasks
        else delayMicroseconds(d);
    }
}

// Holds a send of panel rows [y0, y1) until the scan has passed them, so they show on the next scan whole. The first
// send of a frame anchors it to a scan; every later one must land before that scan's successor reaches its rows, or
// that scan shows the frame half new (counted in lateJobs).
void paceSend(int y0, int y1, uint32_t& anchor) {
    constexpr int kMargin = 16;   // panel rows of slack either side of the scan
    const uint32_t period = tePeriod, fall = teFall, now = micros();
    if (!period || now - fall > 4 * period) { anchor = 0; return; }   // no TE: send at once, as before
    const uint32_t high = teHigh < period / 4 ? teHigh : 0;
    const float line = static_cast<float>(period - high) / W;        // microseconds per panel row
    if (!anchor) anchor = now - fall >= period - kLeadUs ? fall + period : fall;
    waitUntil(anchor + static_cast<int32_t>((y1 + kMargin) * line));
    const int32_t over = static_cast<int32_t>(micros() - anchor - period) - static_cast<int32_t>((y0 - kMargin) * line);
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
            const uint32_t t = micros();
            gfx->draw16bitBeRGBBitmap(j.x, j.y, const_cast<uint16_t*>(j.px), j.w, j.h);
            sendUs += micros() - t;
            sentPx += j.w * j.h;
#ifdef POWER_LOG
            plog::sent(j.w * j.h);
#endif
        } else {
            if (wantBrightness != brightnessNow) {
                brightnessNow = wantBrightness;
                gfx->setBrightness(brightnessNow);
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
    wantBrightness = static_cast<uint8_t>(kFullBrightness * ui::brightness(app, model));
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
        const uint32_t t = micros();
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
        busy += micros() - t;
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
        const uint32_t t = micros();
        ui::Canvas cv = ui::Canvas::panelWindow(stripBuf[k], W, x0, x1, y0, rows);
        ui::draw(cv, app, model, ms, scratch);
        // These panel rows' hashes no longer describe the panel: flip them so the next whole frame sends the rows.
        for (int x = x0; x < x1; ++x) rowHash[x] = ~rowHash[x];
        busy += micros() - t;
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
        if (offReq && !power::off()) { offReq = false; Serial.println("power: no power chip, so no off"); }
        if (++tick % 125 == 0) {
            const power::State p = power::read();
            portENTER_CRITICAL(&pwLock);
            pwShared = p;
            portEXIT_CRITICAL(&pwLock);
        }
#endif
        int16_t x[2], y[2];
        const uint8_t n = touch.getPoint(x, y, 2);
        touchf::Event ev[2];
        const int k = filter.feed(millis(), n, static_cast<int16_t>(W - 1 - y[0]), x[0], ev);
        for (int i = 0; i < k; ++i) {
            const touchf::Kind kind = ev[i].kind;
            if (kind == touchf::Kind::Down && !pressed) { pressed = true; cpuclk::hold(); }
            if ((kind == touchf::Kind::Up || kind == touchf::Kind::Cancel) && pressed) { pressed = false; cpuclk::release(); }
            xQueueSend(touchEvents, &ev[i], 0);
        }
    }
}

bool touchLog = false;   // serial "t": print every touch sample and what each release did

void handleTouch() {
    touchf::Event e;
    while (xQueueReceive(touchEvents, &e, 0) == pdTRUE) {
#ifdef POWER_LOG
        plog::touch();
#endif
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
    static uint16_t* whole = static_cast<uint16_t*>(ps_malloc(W * W * 2));
    constexpr int kReps = 3;
    uint32_t strip[kStrips] = {}, hash = 0, full = 0;
    for (uint32_t& c : ui::gfxCycles) c = 0;
    for (int r = 0; r < kReps; ++r) {
        ui::gfxClock = []() -> uint32_t { return ESP.getCycleCount(); };   // the strips only
        for (int s = 0; s < kStrips; ++s) {
            const int x0 = s * kStripCols, x1 = min(W, x0 + kStripCols);
            uint32_t t = micros();
            ui::Canvas cv = ui::Canvas::panelWindow(stripBuf[0], W, x0, x1, 0, W);
            ui::draw(cv, app, model, ms, scratch);
            strip[s] += micros() - t;
            t = micros();
            volatile uint32_t sink = 0;
            for (int x = x0; x < x1; ++x) sink = sink + hashRow(stripBuf[0] + (x - x0) * W, W);
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
// "p" (profile), "s" (frame stats now), "t" (touch log on/off: every sample, and what each release did). Live build:
// "N {json}", the `nearby` message (spec 8.3) from tools/send_nearby.py, standing in for the app over BLE; "O", the
// board off (against burn-in on a board left plugged in).
void pollSerial(uint32_t ms) {
    static char line[640];
    static int n = 0;
    while (Serial.available()) {
        const char ch = static_cast<char>(Serial.read());
        if (ch != '\n' && n < static_cast<int>(sizeof(line)) - 1) { line[n++] = ch; continue; }
        const int len = n;
        line[n] = 0; n = 0;
#ifdef HANDHELD_LIVE
        if (line[0] == 'N' && line[1] == ' ') {
            Serial.println(live::setNearby(line + 2, len - 2) ? "nearby: ok" : "nearby: rejected");
            continue;
        }
        if (line[0] == 'O') {   // the board off, by whichever task owns the I2C bus
            if (touchOk) offReq = true;
            else if (!power::off()) Serial.println("power: no power chip, so no off");
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
#ifdef POWER_LOG
        else if (line[0] == 'S') { stress::start(ms); continue; }   // the power analysis stress test
        else if (line[0] == 'X') { stress::stop(ms); continue; }
#endif
        else if (line[0] == 't') { touchLog = !touchLog; Serial.printf("touch log %s\n", touchLog ? "on" : "off"); }   // print the frame stats now and start a new window
        app.lastInput = ms;
    }
}

}  // namespace

void setup() {
#ifdef POWER_LOG
    Serial.setTxBufferSize(4096);   // the power log writes whole lines only, so a line must fit
#else
    Serial.setTxBufferSize(1024);   // a burst of lines (the frame stats are three writes) fits while the host reads
#endif
    // Barely wait on USB serial. Plugged into a computer with nothing reading the port, the host stops taking data,
    // and with the 100 ms default a print blocks its task (on core 3.3 up to 2 s, 20 tries: the frame loop's stats
    // line froze the screen for seconds on the first frame after a pause, and every 10 s while scrolling). After 5 ms
    // without progress the core calls the port disconnected and drops output nobody reads until the host reads
    // again. Not 0: core 3.2 then waits forever (its retry count starts at the timeout and wraps), which froze the
    // loop from boot until the port was opened.
    Serial.setTxTimeoutMs(5);
    Serial.setRxBufferSize(1024);   // a `nearby` line is about 250 bytes; the default 256 drops a tail mid-frame
    Serial.begin(115200);
    cpuclk::begin();   // before the network task, which holds full speed through each request
    fullSpeed(true);   // awake from boot
    Wire.begin(kSda, kScl);
    if (!gfx->begin(80000000)) Serial.println("display: begin failed");
    gfx->fillScreen(0);
    bus->beginWrite();
    bus->writeC8D8(0x35, 0x00);   // TE on, vertical blanking only (Arduino_CO5300's init leaves it off)
    bus->endWrite();
    pinMode(kLcdTe, INPUT);
    attachInterrupt(kLcdTe, onTe, CHANGE);
    for (int k = 0; k < 2; ++k) {
        stripBuf[k] = static_cast<uint16_t*>(heap_caps_malloc(W * kStripCols * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        stripFree[k] = xSemaphoreCreateBinary();
        xSemaphoreGive(stripFree[k]);
    }
#ifdef HANDHELD_LIVE
    // PSRAM in the live build: Wi-Fi and a TLS connection need about 100 KB of internal RAM, and these 69 KB don't
    // draw measurably faster inside it (see the README's speed notes).
    scratch = static_cast<uint16_t*>(heap_caps_malloc(ui::kScratchPixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    scratch = static_cast<uint16_t*>(heap_caps_malloc(ui::kScratchPixels * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
    if (!stripBuf[0] || !stripBuf[1] || !scratch) Serial.println("memory: allocation failed");
    // 32 rendered cards (2.2 MB of PSRAM): both tabs' cards, at rest and moving, fit at once, so switching tabs
    // or opening a card doesn't re-render the other tab's cards (16 didn't fit 12 trains and 5 buses).
    constexpr int kCards = 32;
    if (void* cache = ps_malloc(ui::cardCacheBytes(kCards))) ui::setCardCache(cache, kCards);
    touch.setPins(kTpRst, kTpInt);
    touchOk = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, kSda, kScl);
    Serial.printf("reset reason %d (1 power-on, 3 software, 4 panic, 6 task watchdog, 9 brownout)\n",
                  static_cast<int>(esp_reset_reason()));
    Serial.printf("touch: %s, cpu %u MHz, internal free %u, psram free %u\n", touchOk ? "ok" : "failed",
                  getCpuFrequencyMhz(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
    Serial.printf("sdk: arduino %s, esp-idf %s, code runs from %s, data cache line %d B\n", ESP_ARDUINO_VERSION_STR,
                  esp_get_idf_version(),
#ifdef CONFIG_SPIRAM_XIP_FROM_PSRAM
                  "PSRAM (XIP)",
#else
                  "flash",
#endif
                  CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE);
    startUnix = buildUnix();
    startMs = millis();
#ifdef HANDHELD_LIVE
    snap = static_cast<live::Snapshot*>(ps_malloc(sizeof(live::Snapshot)));
    live::clear(*snap);
    Serial.printf("power: %s\n", power::begin() ? "AXP2101 ok" : "AXP2101 not found");
    pw = pwShared = power::read();   // before the touch task starts; after that only it reads the bus
    mock::fromLive(model, *snap, pw, startUnix, false);
    live::start();
#ifdef POWER_LOG
    plog::begin();
#endif
#else
    mock::update(model, startUnix, startUnix);
#endif
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

bool inMotion = false;   // the last frame drawn was part of an animation

void loop() {
    static uint32_t lastData = 0, lastFrame = 0, frames = 0, statT = 0, sumDraw = 0, sumFrame = 0;
    // In motion, each frame starts just before a scan does, so frames show at a steady one or two scans each (a frame
    // drawn mid-scan can miss its scan, and the next can't, which judders); before the clock is read, so motion matches.
    if (inMotion && tePeriod && micros() - teFall < 4 * tePeriod) waitUntil(teFall + tePeriod - kLeadUs);
    if (uxQueueMessagesWaiting(touchEvents)) fullSpeed(true);   // a touch: full speed before anything answers it
    handleTouch();
    const uint32_t ms = millis();   // after the touch events, so none is stamped later than this frame
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
        bool night = lm >= cta::kNightStartMin && lm < cta::kNightEndMin;
#ifdef POWER_LOG
        if (stress::running()) night = false;   // stress-v2: daytime polling during a run, whatever the hour
#endif
        live::setSlow(model.docked || night);
        // The battery log (spec 8.5): every 15 min, and once at boot.
        static uint32_t lastLog = 0;
        if (lastLog == 0 || ms - lastLog >= 15 * 60 * 1000) {
            lastLog = ms;
            Serial.printf("power: usb %d, battery %s %d%% %u mV%s; docked %d\n", pw.usb, pw.battery ? "yes" : "no",
                          pw.percent, pw.battMv, pw.charging ? ", charging" : "", model.docked);
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
    ui::tick(app, model, ms);
#ifdef HANDHELD_LIVE
    static bool wasDim = false;   // a tap on the dim screen is a wake (spec 8.2)
    if (wasDim && !app.dim) live::wake();
    wasDim = app.dim;
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
    const uint32_t t0 = micros();
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
    sumFrame += micros() - t0;
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
        k += snprintf(line + k, sizeof(line) - k, ", wifi %d, CTA requests %u%s%s", snap->wifiUp,
                      static_cast<unsigned>(snap->requests), snap->status[0] ? ", " : "", snap->status);
#endif
        snprintf(line + k, sizeof(line) - k, "\r\n");
        Serial.print(line);
        frames = sumDraw = sumFrame = 0;
        sentPx = 0;
        sendUs = 0;
        lateJobs = 0;
        statT = ms;
    }
}
