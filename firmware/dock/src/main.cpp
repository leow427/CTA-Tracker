// The dock on the device: the frame loop and the panel's interrupts on core 1 (touch in, the UI drawn and shown on
// the panel, the LED rows), the network task and the touch task on core 0.
//
// Frames are drawn in bands of portrait rows in internal RAM, already in the landscape panel's order, and copied into
// its hidden frame buffer (display.cpp, bands.cpp); a frame is drawn when something changed: new data, a touch, the
// clock's minute, or the half-second bob and blink. Each frame's drawing is recorded once (gfx2d's ui::Recording) and
// played band by band, and only into the bands the hidden buffer doesn't already hold: at rest, Trackie's bob and the
// blinks alternate with the two buffers, so a frame at rest draws nothing (dock performance analysis, round 1).
//
// Serial commands (115200): "m" steps Trackie through every mood and back to live (spec 6.3's debug cycle, until the
// two-finger gesture exists), "s" shows the next screen (stops, map, picker), "h" the stops screen, "p" prints the picks, "d" puts the
// default picks back, "g" times the drawing primitives over the next frame. Tearing diagnostics: "0" all off, "1" freeze
// the screen, "2" a static test pattern, "3" Wi-Fi off/on, "4" single buffer, "5" the next panel timing, "6" the panel restart after a late frame off/on; "f" sends the frame on screen
// (tools/frame_grab.py), "l" prints the LED colors, "w" checks that the dock's Wi-Fi network is on the
// air, "x" swaps sections 1 and 2 (a real flash write), "z" powers down (LEDs and backlight off, deep sleep).
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <time.h>

#include <new>

#include "cta_config.h"
#include "display.h"
#include "dock_leds.h"
#include "dock_ui.h"
#include "json_heap.h"
#include "leds.h"
#include "line_colors.h"
#include "local_time.h"
#include "net.h"
#include "pins.h"
#include "store.h"
#ifdef PERF_LOG
#include "perf_log.h"
#include "stress.h"
#endif

namespace {

dock::Data* data;
dock::PickerLists* lists;
dock::App app;
cta::PickedStop picks[cta::kDockStops];
uint32_t dataVersion = 0, listsVersion = 0;
bool profileNext = false, printLeds = false;
bool wifiOffDiag = false, forceFrame = false;
#ifdef PERF_LOG
// The perf log's view of the loop: a picker list asked for (and when), and boot milestones (ms since boot, 0 = not yet).
dock::Request listWanted = dock::Request::None;
uint32_t listAskedMs = 0;
uint32_t bootWifiMs = 0, bootClockMs = 0, bootLoadedMs = 0;
#endif

// ---------- diagnostics for the tearing (keys 0–5) ----------
// The mode's name in the bottom left, so Leo sees which one is on.
char diagStep = 0;   // the test step's letter (serial A–Z), shown large so Leo knows which step is on

void diagLabel(ui::Canvas& cv) {
    char buf[96];
    if (diagStep) {
        const char step[8] = {'S', 'T', 'E', 'P', ' ', diagStep, 0};
        const ui::Font* fs = ui::Canvas::font(900, 34);
        const float sw = ui::Canvas::measure(fs, step) + 24;
        cv.fillRect(8, dock::H - 96, sw, 50, 0xffd95a);
        cv.text(fs, step, 20, dock::H - 71, 0x000000, ui::Align::Left, ui::Base::Middle);
    }
    const display::Diag d = display::diag();
    const char* mode = d == display::Diag::Freeze ? "FREEZE" : d == display::Diag::Pattern ? "PATTERN"
                     : d == display::Diag::Single ? "1 BUFFER" : nullptr;
    if (!mode && !wifiOffDiag && display::timing() == 0) return;
    snprintf(buf, sizeof(buf), "DIAG %s%s%s T%d", mode ? mode : "", wifiOffDiag ? " WIFI OFF" : "", mode || wifiOffDiag ? "," : "",
             display::timing());
    const ui::Font* f = ui::Canvas::font(900, 16);
    const float w = ui::Canvas::measure(f, buf) + 16;
    cv.fillRect(8, dock::H - 40, w, 30, 0x000000);
    cv.text(f, buf, 16, dock::H - 25, 0xffd95a, ui::Align::Left, ui::Base::Middle);
}

// A fine grid: 1 px lines every 8 px both ways, brighter every 40 px, a frame around the edge, and the time it
// was drawn. It's never redrawn, so any shift, jitter or tear is the panel's.
void drawPattern(ui::Canvas& cv) {
    cv.clear(0x000000);
    for (int x = 0; x < dock::W; x += 8) cv.fillRect(x, 0, 1, dock::H, x % 40 ? 0x606060 : 0xffffff);
    for (int y = 0; y < dock::H; y += 8) cv.fillRect(0, y, dock::W, 1, y % 40 ? 0x606060 : 0xffffff);
    cv.fillRect(0, 0, dock::W, 2, 0xff0000);
    cv.fillRect(0, dock::H - 2, dock::W, 2, 0xff0000);
    cv.fillRect(0, 0, 2, dock::H, 0x00ff00);
    cv.fillRect(dock::W - 2, 0, 2, dock::H, 0x00ff00);
    char buf[48];
    snprintf(buf, sizeof(buf), "PATTERN at %u ms", static_cast<unsigned>(millis()));
    cv.fillRect(150, 380, 180, 40, 0x000000);
    cv.text(ui::Canvas::font(900, 16), buf, dock::W / 2.f, 400, 0xffffff, ui::Align::Center, ui::Base::Middle);
    diagLabel(cv);
}

void printDiag() {
    const display::Diag d = display::diag();
    Serial.printf("diag: %s, Wi-Fi %s, timing %d (%s), %u panel restarts\n",
                  d == display::Diag::Freeze ? "freeze" : d == display::Diag::Pattern ? "pattern" : d == display::Diag::Single ? "single buffer" : "normal",
                  wifiOffDiag ? "off" : "on", display::timing(), display::timingName(display::timing()), display::recoveries());
}
uint32_t cycles() { return ESP.getCycleCount(); }

// Big JSON documents (bus patterns, picker lists) in PSRAM: internal RAM is for Wi-Fi and TLS.
void* psAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void psFree(void* p) { heap_caps_free(p); }
void* psRealloc(void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
const cta::JsonHeap kPsramHeap = {psAlloc, psFree, psRealloc};

int localMinute(uint32_t now) {
    const time_t t = static_cast<time_t>(now);
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

void printPicks() {
    for (int i = 0; i < cta::kDockStops; ++i) {
        const cta::PickedStop& p = picks[i];
        if (!p.id[0]) Serial.printf("pick %d: empty\n", i + 1);
        else Serial.printf("pick %d: %s %s %s, %s (%s)\n", i + 1, p.type == cta::Kind::Bus ? "bus" : "train", p.route,
                           p.id, p.name, p.dir);
    }
}

void setPicks() {
    bool wrote = false;   // unchanged picks never touch flash
    if (!store::savePicks(picks, &wrote)) Serial.println("store: saving the picks failed");
    net::setPicks(picks);
    printPicks();
}

void pollSerial() {
    while (Serial.available()) {
        const int c = Serial.read();
#ifdef PERF_LOG
        if (c == 'S') { stress::start(); continue; }   // the stress test (dock performance analysis)
        if (c == 'X') { stress::stop(); continue; }
#endif
        if (c == 'm') {
            app.debugMood = app.debugMood >= static_cast<int>(cta::Mood::Hyped) ? -1 : app.debugMood + 1;
            Serial.printf("debug: mood %d%s\n", app.debugMood, app.debugMood < 0 ? " (live)" : "");
        } else if (c == 'p') {
            printPicks();
        } else if (c == 'h') {   // the stops screen
            app.screen = dock::Screen::Home;
            Serial.println("debug: screen 0");
        } else if (c == 's') {   // the next screen: stops, map, picker
            app.screen = app.screen == dock::Screen::Home ? dock::Screen::Map
                       : app.screen == dock::Screen::Map ? dock::Screen::Picker : dock::Screen::Home;
            app.picker.step = 1;
            app.panX = app.panY = 0;
            Serial.printf("debug: screen %d\n", static_cast<int>(app.screen));
        } else if (c == 'x') {   // swaps sections 1 and 2: a real flash write, for checking the panel through one
            const cta::PickedStop t = picks[0];
            picks[0] = picks[1];
            picks[1] = t;
            setPicks();
        } else if (c == '6') {
            display::setAutoRecover(!display::autoRecovering());
            Serial.printf("diag: panel restart after a late frame %s\n", display::autoRecovering() ? "on" : "off");
        } else if (c >= '0' && c <= '5') {
            if (c == '0') { display::setDiag(display::Diag::Normal); wifiOffDiag = false; net::setWifiOff(false); display::setTiming(display::bootTiming()); }
            if (c == '1') display::setDiag(display::Diag::Freeze);
            if (c == '2') display::setDiag(display::Diag::Pattern);
            if (c == '3') { wifiOffDiag = !wifiOffDiag; net::setWifiOff(wifiOffDiag); }
            if (c == '4') display::setDiag(display::Diag::Single);
            if (c == '5') display::setTiming((display::timing() + 1) % display::timingCount());
            forceFrame = true;
            printDiag();
        } else if ((c >= 'A' && c <= 'Z') || c == '.') {   // test step letter on screen ('.' clears it)
            diagStep = c == '.' ? 0 : static_cast<char>(c);
            forceFrame = true;
            Serial.printf("diag: step %c\n", c);
        } else if (c == 'w') {
            net::checkWifi();
        } else if (c == 'l') {
            printLeds = true;
        } else if (c == 'f') {
            display::dumpFrame();
        } else if (c == 'g') {
            profileNext = true;
        } else if (c == 'd') {
            cta::defaultPicks(picks);
            setPicks();
        } else if (c == 'z') {   // power down: LEDs and backlight off, then deep sleep until reset or power cycle
            Serial.println("dock: powering down (reset or replug to wake)");
            cta::LedRow off[cta::kLedRows];
            leds::post(off, 0, true);   // night: every LED black
            delay(300);
            display::backlight(false);
            Serial.flush();
            esp_deep_sleep_start();
        }
    }
}

}  // namespace

void setup() {
#ifdef PERF_LOG
    // A TX buffer, so the perf log's whole lines (up to about 400 bytes) go out in the background: without one, a
    // line only fits the UART's 128-byte FIFO and a blocking write would hold the loop for 20 ms.
    Serial.setTxBufferSize(4096);
#endif
    Serial.begin(115200);
    delay(200);
#ifdef PERF_LOG
    perf::begin();
#endif
    Serial.printf("\ndock: reset reason %d, internal free %u, psram free %u\n", static_cast<int>(esp_reset_reason()),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
    // As on the handheld: the task watchdog warns instead of rebooting, and doesn't watch the idle tasks. Core 0's
    // idle task can wait out a train API handshake (about 2.5 s of ECDSA in software) behind the network task, and a
    // reboot mid-demo would cost more than a warning.
    const esp_task_wdt_config_t wdt = {.timeout_ms = 5000, .idle_core_mask = 0, .trigger_panic = false};
    esp_task_wdt_reconfigure(&wdt);
    setenv("TZ", cta::kTz, 1);
    tzset();
    cta::setJsonHeap(&kPsramHeap);
    data = new (psAlloc(sizeof(dock::Data))) dock::Data();
    lists = new (psAlloc(sizeof(dock::PickerLists))) dock::PickerLists();
    if (!data || !lists) Serial.println("memory: allocation failed");
    leds::begin();
    if (!display::begin()) Serial.println("display: begin failed");
    store::loadPicks(picks);
    printPicks();
    net::start(picks);
    Serial.printf("dock: up; internal free %u, psram free %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
}

void loop() {
    static uint32_t lastHalf = UINT32_MAX, lastMinute = UINT32_MAX, lastLeds = 0, statT = 0, frames = 0, sumDraw = 0;
    static bool dirty = true;
#ifdef PERF_LOG
    // The perf log's picture of the data: picked sections, those with arrivals, the oldest arrivals' age.
    int picked = 0, withData = 0, oldest = -1;
    {
        const uint32_t now = net::unixNow();
        for (const dock::Section& s : data->sec) {
            if (!s.pick.id[0]) continue;
            ++picked;
            if (!s.rec.ts) continue;
            ++withData;
            const int age = now > s.rec.ts ? static_cast<int>(now - s.rec.ts) : 0;
            if (age > oldest) oldest = age;
        }
        const uint32_t m = millis();
        if (!bootWifiMs && data->wifiUp) bootWifiMs = m;
        if (!bootClockMs && now) bootClockMs = m;
        if (!bootLoadedMs && now && picked && withData == picked) bootLoadedMs = m;
        static uint8_t lastPhase = 0;
        stress::tick(app, m, now && picked && withData == picked);
        if (stress::phase() == 1 && lastPhase == 0)
            perf::event("boot,wifi=%u,clock=%u,loaded=%u", static_cast<unsigned>(bootWifiMs), static_cast<unsigned>(bootClockMs),
                        static_cast<unsigned>(bootLoadedMs));
        lastPhase = stress::phase();
    }
#endif
    // touch
    display::Touch t;
#ifdef PERF_LOG
    while (display::nextTouch(t) || stress::nextTouch(t)) {
        perf::touch();
        if (t.kind == display::Touch::Up) perf::tapUp(t.ms);
        else if (t.kind == display::Touch::Move) perf::move(t.ms);
#else
    while (display::nextTouch(t)) {
#endif
        if (t.kind != display::Touch::Move)   // for checking the touch mapping: where a press starts and ends
            Serial.printf("touch: %s %d,%d (%d finger%s)\n", t.kind == display::Touch::Down ? "down" : "up", t.x, t.y,
                          t.fingers, t.fingers == 1 ? "" : "s");
        if (t.kind == display::Touch::Down) dock::pointerDown(app, t.x, t.y, t.ms);
        else if (t.kind == display::Touch::Move) dock::pointerMove(app, *lists, t.x, t.y, t.ms);
        else dock::pointerUp(app, *data, *lists, t.x, t.y, t.ms);
        dirty = true;
    }
    const uint32_t ms = millis();   // after the touch events, so none is stamped later than this frame
    dock::tick(app, ms);
    pollSerial();
    // data
    bool dataChanged = false;
    if (net::read(*data, dataVersion)) {
        dirty = dataChanged = true;
        // A snapshot from before the network task took up a new pick still has the old stop: show the new pick, empty,
        // until one that knows it arrives (never the old stop for a frame).
        for (int i = 0; i < cta::kDockStops; ++i)
            if (!cta::samePick(data->sec[i].pick, picks[i])) {
                data->sec[i] = dock::Section();
                data->sec[i].pick = picks[i];
                data->busPathN[i] = 0;
            }
    }
    if (net::readLists(*lists, listsVersion)) {
        dirty = true;
#ifdef PERF_LOG
        const dock::Load st = listWanted == dock::Request::Routes ? lists->routesState
                            : listWanted == dock::Request::Dirs ? lists->dirsState
                            : listWanted == dock::Request::Stops ? lists->stopsState : dock::Load::Loading;
        if (listWanted != dock::Request::None && st != dock::Load::Loading) {
            perf::listLoaded(millis() - listAskedMs);
            listWanted = dock::Request::None;
        }
#endif
    }
    if (app.request != dock::Request::None) {
        // Show "Loading" at once; the network task publishes the list when it's in.
        if (app.request == dock::Request::Routes) lists->routesState = dock::Load::Loading;
        if (app.request == dock::Request::Dirs) lists->dirsState = dock::Load::Loading;
        if (app.request == dock::Request::Stops) lists->stopsState = dock::Load::Loading;
        net::request(app.request, app.picker.line, app.picker.dirName);
#ifdef PERF_LOG
        listWanted = app.request;
        listAskedMs = millis();
#endif
        app.request = dock::Request::None;
        dirty = true;
    }
    if (app.picked) {
        app.picked = false;
        picks[app.pickedSection] = app.pick;
        setPicks();
        // Show the new pick at once, without the old stop's data, until the network task's next snapshot.
        dock::Section& s = data->sec[app.pickedSection];
        s = dock::Section();
        s.pick = app.pick;
        data->busPathN[app.pickedSection] = 0;
        dirty = dataChanged = true;
    }
    // the clock and night hours
    dock::Clock clock;
    clock.now = net::unixNow();
    clock.night = clock.now && cta::inNightHours(localMinute(clock.now), cta::kNightStartMin, cta::kNightEndMin);
#ifdef PERF_LOG
    if (stress::running()) clock.night = false;   // runs keep daytime polling and look at any hour
#endif
    net::setSlow(clock.night);
    if (ms / 500 != lastHalf) { lastHalf = ms / 500; dirty = true; }
    if (clock.now / 60 != lastMinute) { lastMinute = clock.now / 60; dirty = true; }
    // the frame
    if (dirty || forceFrame || dock::animating(app)) {
        dirty = forceFrame = false;
        const bool profile = profileNext;
        if (profile) {
            profileNext = false;
            memset(ui::gfxCycles, 0, sizeof(ui::gfxCycles));
            ui::gfxClock = cycles;
        }
        const uint32_t t0 = millis();
        struct Ctx { const dock::Clock* clock; uint32_t ms; } ctx = {&clock, ms};
        if (display::diag() == display::Diag::Pattern)
            display::render([](ui::Canvas& cv, void*) { drawPattern(cv); }, nullptr);
        else
            display::render([](ui::Canvas& cv, void* p) {
                const Ctx& c = *static_cast<Ctx*>(p);
                dock::draw(cv, app, *data, *lists, *c.clock, c.ms);
                diagLabel(cv);
            }, &ctx);
        sumDraw += millis() - t0;
        if (profile) {
            ui::gfxClock = nullptr;
            Serial.printf("profile: frame %u ms;", static_cast<unsigned>(millis() - t0));
            for (int k = 0; k < static_cast<int>(ui::Prim::Count); ++k)
                Serial.printf(" %s %u", ui::kPrimNames[k], static_cast<unsigned>(ui::gfxCycles[k] / 240000));
            uint32_t du, cu, wu;
            display::lastTimes(du, cu, wu);
            Serial.printf(" (ms); draw %u, copy %u, panel switch %u ms\n", static_cast<unsigned>(du / 1000),
                          static_cast<unsigned>(cu / 1000), static_cast<unsigned>(wu / 1000));
        }
        ++frames;
#ifdef PERF_LOG
        {
            uint32_t du, cu, wu;
            display::lastTimes(du, cu, wu);
            perf::frame(du, cu, wu, display::lastBands(), dock::animating(app), millis(), display::lastBandsDrawn());
        }
#endif
    }
    // the LED rows: their state goes to the LED task (leds.cpp), which breathes LED 7 on its own 25 Hz tick
    if (ms - lastLeds >= 200 || dataChanged) {
        lastLeds = ms;
        cta::LedRow rows[cta::kLedRows];
        for (int i = 0; i < cta::kLedRows; ++i) {
            const dock::Section& s = data->sec[i];
            if (!s.pick.id[0]) continue;
            rows[i].mask = s.mask;
            rows[i].rgb = s.pick.type == cta::Kind::Bus ? cta::kBusColor : cta::lineColor(s.pick.route);
            rows[i].ts = s.posTs;
            rows[i].due = dock::dueNow(s, clock.now);
        }
        leds::post(rows, clock.now, clock.night);
        if (printLeds) {
            printLeds = false;
            uint32_t rgb[cta::kLedCount];
            cta::ledFrame(rows, clock.now, clock.night, LED_MAX_BRIGHTNESS, ms, rgb);
            for (int r = 0; r < cta::kLedRows; ++r) {
                Serial.printf("leds row %d (LED 1..7):", r + 1);
                for (int i = 0; i < cta::kLedsPerRow; ++i) Serial.printf(" %06X", static_cast<unsigned>(rgb[cta::ledIndex(r, i)]));
                Serial.println();
            }
        }
    }
    static uint32_t scanT = 0;
    // The panel's scan: late refills show as a shaking, torn picture. Every second for the first 2 minutes (boot is
    // when the radio, flash and network are busiest), then every 5 s.
#ifdef PERF_LOG
    if (ms - scanT >= 1000u) {   // every second, for the perf log
#else
    if (ms - scanT >= (ms < 120000 ? 1000u : 5000u)) {
#endif
        scanT = ms;
        uint32_t f, late, gap, period;
        display::scanStats(f, late, gap, period);
#ifdef PERF_LOG
        perf::scan(late);
#endif
        if (late) Serial.printf("scan: at %u ms, %u frames, %u late, longest %u us (period %u us)\n", ms, f, late, gap, period);
    }
    if (ms - statT >= 30000) {
        statT = ms;
        Serial.printf("stats: %u frames, %u ms each, %u switch timeouts; internal free %u (min %u), psram free %u; %u CTA requests\n",
                      frames, frames ? sumDraw / frames : 0, display::timeouts(),
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                      ESP.getFreePsram(), net::requests());
        frames = sumDraw = 0;
    }
#ifdef PERF_LOG
    if (perf::due(ms)) {
        perf::Inputs in;
        in.phase = stress::phase();
        in.picked = picked;
        in.sections = withData;
        in.dataAgeSec = oldest;
        in.screen = static_cast<uint8_t>(app.screen);
        perf::sample(ms, in);
    }
    perf::flush();
#endif
    delay(2);
}
