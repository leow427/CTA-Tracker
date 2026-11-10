// The handheld on the device: display (CO5300 over QSPI), touch (CST9217), and the frame loop. The live build
// (HANDHELD_LIVE, env:live) feeds the screens real CTA data from the network task (live.*) and reads the power chip
// (power.*); the mock build simulates arrivals (mock_data.*).
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
#include "cpu_clock.h"
#include "mock_data.h"
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
#ifdef POWER_LOG
    static uint16_t frameSeq = 0;
    ++frameSeq;
    const bool lit = plog::litWanted();
#endif
    for (int s = 0; s < kStrips; ++s) {
        const int k = s & 1, y0 = s * kStripRows, rows = y0 + kStripRows <= W ? kStripRows : W - y0;
        xSemaphoreTake(stripFree[k], portMAX_DELAY);   // its last send has finished
        const uint32_t t = micros();
        uint16_t* buf = stripBuf[k];
        ui::Canvas cv = ui::Canvas::panelBand(buf, W, y0, rows);
        ui::draw(cv, app, model, ms, scratch);
#ifdef POWER_LOG
        if (lit) plog::litStrip(buf, W * rows);
#endif
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
#ifdef POWER_LOG
    if (lit) plog::litDone();
    plog::frameQueued(frameSeq);
    const Job end{nullptr, static_cast<int16_t>(frameSeq), 0, 0, 0, -1};
#else
    const Job end{nullptr, 0, 0, 0, 0, -1};
#endif
    xQueueSend(jobs, &end, portMAX_DELAY);
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

// The loop's hold on full speed (cpu_clock.h). It lets go only once the dim always-on screen is on the panel, with no
// finger down; that screen changes a little twice a second. A tap there wakes it on release, by which time the press
// has taken the hold back (here, and in the touch task), so the wake is drawn exactly as fast as before.
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
// "p" (profile), "s" (frame stats now), "t" (touch log on/off: every sample, and what each release did). Live build:
// "N {json}", the `nearby` message (spec 8.3) from tools/send_nearby.py, standing in for the app over BLE.
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
#endif
    Serial.setRxBufferSize(1024);   // a `nearby` line is about 250 bytes; the default 256 drops a tail mid-frame
    Serial.begin(115200);
    cpuclk::begin();   // before the network task, which holds full speed through each request
    fullSpeed(true);   // awake from boot
    Wire.begin(kSda, kScl);
    if (!gfx->begin(80000000)) Serial.println("display: begin failed");
    gfx->fillScreen(0);
    for (int k = 0; k < 2; ++k) {
        stripBuf[k] = static_cast<uint16_t*>(heap_caps_malloc(W * kStripRows * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
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
    constexpr int kCards = 16;
    if (uint16_t* cache = static_cast<uint16_t*>(ps_malloc(kCards * ui::kScratchPixels * 2))) ui::setCardCache(cache, kCards);
    touch.setPins(kTpRst, kTpInt);
    touchOk = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, kSda, kScl);
    Serial.printf("reset reason %d (1 power-on, 3 software, 4 panic, 6 task watchdog, 9 brownout)\n",
                  static_cast<int>(esp_reset_reason()));
    Serial.printf("touch: %s, cpu %u MHz, internal free %u, psram free %u\n", touchOk ? "ok" : "failed",
                  getCpuFrequencyMhz(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
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

void loop() {
    static uint32_t lastData = 0, lastFrame = 0, frames = 0, statT = 0, sumDraw = 0, sumFrame = 0;
    if (uxQueueMessagesWaiting(touchEvents)) fullSpeed(true);   // a touch: full speed before anything answers it
    handleTouch();
    const uint32_t ms = millis();   // after the touch events, so none is stamped later than this frame
    pollSerial(ms);
    if (ms - lastData >= 1000 || lastData == 0) {
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
        live::setSlow(model.docked || (lm >= cta::kNightStartMin && lm < cta::kNightEndMin));
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
    // Every input of this frame is in: full speed for anything but the dim always-on screen left alone, once it's on
    // the panel. The frame that dims to it is drawn at full speed too, so the full-brightness moment before the
    // dimmed brightness applies (at the end of the frame) lasts as long as before. The profile runs at full speed so
    // its times compare with the README's (and its cycle counts convert at 240 MHz).
    static bool aodDrawn = false;   // the last frame drawn was the always-on screen
    const bool aod = ui::alwaysOn(app);
    fullSpeed(!aod || !aodDrawn || app.drag.down || autoScrollUntil != 0 || profileReq);
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
    aodDrawn = aod;
    ++frames;
#ifdef POWER_LOG
    plog::frame(moving, ms);
#endif
    if (ms - statT > 10000 || statsReq) {
        statsReq = false;
        const uint32_t f = frames ? frames : 1;
        Serial.printf("fps %.1f; per frame: draw %u us, loop %u us, send %u us, %u px (of %u); ",
                      frames * 1000.f / (ms - statT), sumDraw / f, sumFrame / f, sendUs / f, sentPx / f, W * W);
        Serial.printf("scenario %s, mood %d, %d trains, %d buses, internal free %u (min %u)", mock::scenarioName(model.scenario),
                      static_cast<int>(model.mood), model.nTrains, model.nBuses, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
#ifdef HANDHELD_LIVE
        Serial.printf(", wifi %d, CTA requests %u%s%s", snap->wifiUp, static_cast<unsigned>(snap->requests),
                      snap->status[0] ? ", " : "", snap->status);
#endif
        Serial.println();
        frames = sumDraw = sumFrame = 0;
        sentPx = 0;
        sendUs = 0;
        statT = ms;
    }
}
