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
// two-finger gesture exists), "s" shows the next screen (stops, map, picker, settings), "h" the stops screen, "p" prints the picks, "d" puts the
// default picks back, "g" times the drawing primitives over the next frame. Tearing diagnostics: "0" all off, "1" freeze
// the screen, "2" a static test pattern, "3" Wi-Fi off/on, "4" single buffer, "5" the next panel timing, "6" the panel restart after a late frame off/on; "f" sends the frame on screen
// (tools/frame_grab.py), "l" prints the LED colors, "w" checks that the dock's Wi-Fi network is on the
// air, "x" swaps sections 1 and 2 (a real flash write), "k" fake arrivals and LED positions on/off (no CTA calls), "z" powers down (LEDs and backlight off, deep sleep).
// The link (ecosystem spec 5.4): "R" forgets the token and reboots to pairing (as POST /reset and three fingers held
// 10 s anywhere on screen do); "T <32 hex>" sets the household token by hand, as POST /pair does (never echoed).
#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clock_ms.h"
#include "serial_out.h"
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <time.h>

#include <new>

#include "card_theme.h"
#include "cta_config.h"
#include "display.h"
#include "dock_leds.h"
#include "dock_status.h"
#include "dock_ui.h"
#include "json_heap.h"
#include "leds.h"
#include "line_colors.h"
#include "link.h"
#include "local_time.h"
#include "net.h"
#include "pet_palette.h"
#include "pet_sprites.h"
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
// The link's state: what GET /config and /info serve, and what the app set.
cta::ConfigMsg linkCfg;   // stops = picks; pet and settings once saved or sent
cta::InfoMsg info = {};
uint16_t petPalette[pet::kPaletteSize];   // Trackie's colors: the default with the app's two swapped in
uint32_t settingsSaveMs = 0;   // Settings' last − / + tap waiting to be saved (0 = none)
constexpr uint32_t kSettingsSaveDelayMs = 2000;   // taps in a row write flash once
constexpr uint32_t kResetHoldMs = 10000;   // three fingers held this long anywhere: the hidden reset
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
    snprintf(buf, sizeof(buf), "PATTERN at %u ms", static_cast<unsigned>(nowMs()));
    cv.fillRect(150, 380, 180, 40, 0x000000);
    cv.text(ui::Canvas::font(900, 16), buf, dock::W / 2.f, 400, 0xffffff, ui::Align::Center, ui::Base::Middle);
    diagLabel(cv);
}

void printDiag() {
    const display::Diag d = display::diag();
    uprintf("diag: %s, Wi-Fi %s, timing %d (%s), %u panel restarts\n",
                  d == display::Diag::Freeze ? "freeze" : d == display::Diag::Pattern ? "pattern" : d == display::Diag::Single ? "single buffer" : "normal",
                  wifiOffDiag ? "off" : "on", display::timing(), display::timingName(display::timing()), display::recoveries());
}
// UART0 (the CH343 port) through its driver, so serial commands are read without waiting. Output (uprintf,
// serial_out.h) waits for the UART; the perf build adds a 4 KB TX buffer, which perf::flush fills with whole
// lines (perf_log.cpp).
constexpr uart_port_t kUart = UART_NUM_0;
void flushSerial() {
    uart_wait_tx_done(kUart, pdMS_TO_TICKS(1000));
}

uint32_t cycles() { return esp_cpu_get_cycle_count(); }

// cJSON's nodes (and the big model structs) in PSRAM, internal only if PSRAM runs out: internal RAM is for Wi-Fi
// and TLS.
void* psAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
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
        if (!p.id[0]) uprintf("pick %d: empty\n", i + 1);
        else uprintf("pick %d: %s %s %s, %s (%s)\n", i + 1, p.type == cta::Kind::Bus ? "bus" : "train", p.route,
                           p.id, p.name, p.dir);
    }
}

// The link's view of the dock: GET /config (stops, pet, settings) and GET /info (newest arrivals, picks_ver).
void publishLink() {
    memcpy(linkCfg.stops, picks, sizeof(picks));
    linkCfg.hasStops = true;
    lan::setConfig(linkCfg);
    for (const dock::Section& s : data->sec)
        if (s.rec.ts > info.lastUpdate) info.lastUpdate = s.rec.ts;
    info.hasSettings = true;   // as the wearable's: the app adopts a change made on the dock (ecosystem spec 5.2)
    info.settings = linkCfg.settings;
    lan::setInfo(info);
}

void setPicks() {
    bool wrote = false;   // unchanged picks never touch flash
    if (!store::savePicks(picks, &wrote)) uprintf("store: saving the picks failed\n");
    net::setPicks(picks);
    ++info.picksVer;   // every pick, from the screen or the app (ecosystem spec 5.3)
    publishLink();
    printPicks();
}

// Sections whose data is still for an older pick show the new pick, empty, until the network task's next snapshot
// knows it (never the old stop for a frame).
void clearStale() {
    for (int i = 0; i < cta::kDockStops; ++i)
        if (!cta::samePick(data->sec[i].pick, picks[i])) {
            data->sec[i] = dock::Section();
            data->sec[i].pick = picks[i];
            data->busPathN[i] = 0;
        }
}

void applyPet() {
    memcpy(petPalette, pet::kDefaultPalette565, sizeof(petPalette));
    if (linkCfg.hasPet) pet::applyColors(petPalette, linkCfg.pet.colors);
    ui::petPalette565 = petPalette;   // drawPet reads it; the next frame (dirty after any write) redraws Trackie
}

// The backlight is on or off only (the CH422G's EXIO2): screen 0 turns it off, anything else on; dock::draw dims the
// frame in software (cta::dockVeil).
void applySettings() { display::backlight(linkCfg.settings.screen > 0); }

// Forgets the token, then boots fresh, showing a pairing code (POST /reset, serial "R", the three-finger hold).
void resetLink(const char* why) {
    uprintf("link: reset (%s): forgetting the token, rebooting\n", why);
    store::wipeLink();
    flushSerial();
    esp_restart();
}

// Serial "T <32 lowercase hex>": the household token, saved and used at once. Never echoed.
void readToken() {
    // The whole line (up to 1 s, as Arduino's readStringUntil waited), so nothing left over runs as commands.
    char s[80];
    size_t n = 0;
    const uint32_t t0 = nowMs();
    for (uint8_t c; nowMs() - t0 < 1000;) {
        if (uart_read_bytes(kUart, &c, 1, pdMS_TO_TICKS(10)) != 1) continue;
        if (c == '\n') break;
        if (n < sizeof(s) - 1) s[n++] = static_cast<char>(c);
    }
    s[n] = 0;
    const char* b = s;   // trimmed, as String::trim did
    while (*b == ' ' || *b == '\t' || *b == '\r') ++b;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
    bool ok = strlen(b) == cta::kTokenLen;
    for (size_t i = 0; ok && b[i]; ++i) ok = (b[i] >= '0' && b[i] <= '9') || (b[i] >= 'a' && b[i] <= 'f');
    if (!ok) { uprintf("link: a token is 32 lowercase hex characters; not set\n"); return; }
    if (!store::saveToken(b)) uprintf("store: saving the token failed\n");
    lan::setToken(b);
    uprintf("link: token set\n");
}

void pollSerial() {
    for (uint8_t c; uart_read_bytes(kUart, &c, 1, 0) == 1;) {
#ifdef PERF_LOG
        if (c == 'S') { stress::start(); continue; }   // the stress test (dock performance analysis)
        if (c == 'X') { stress::stop(); continue; }
#endif
        if (c == 'm') {
            app.debugMood = app.debugMood >= static_cast<int>(cta::Mood::Hyped) ? -1 : app.debugMood + 1;
            uprintf("debug: mood %d%s\n", app.debugMood, app.debugMood < 0 ? " (live)" : "");
        } else if (c == 'p') {
            printPicks();
        } else if (c == 'h') {   // the stops screen
            app.screen = dock::Screen::Home;
            uprintf("debug: screen 0\n");
        } else if (c == 's') {   // the next screen: stops, map, picker, settings
            app.screen = app.screen == dock::Screen::Home ? dock::Screen::Map
                       : app.screen == dock::Screen::Map ? dock::Screen::Picker
                       : app.screen == dock::Screen::Picker ? dock::Screen::Settings : dock::Screen::Home;
            app.pairAsk = false;
            app.picker.step = 1;
            app.panX = app.panY = 0;
            uprintf("debug: screen %d\n", static_cast<int>(app.screen));
        } else if (c == 'x') {   // swaps sections 1 and 2: a real flash write, for checking the panel through one
            const cta::PickedStop t = picks[0];
            picks[0] = picks[1];
            picks[1] = t;
            setPicks();
        } else if (c == '6') {
            display::setAutoRecover(!display::autoRecovering());
            uprintf("diag: panel restart after a late frame %s\n", display::autoRecovering() ? "on" : "off");
        } else if (c >= '0' && c <= '5') {
            if (c == '0') { display::setDiag(display::Diag::Normal); wifiOffDiag = false; net::setWifiOff(false); display::setTiming(display::bootTiming()); }
            if (c == '1') display::setDiag(display::Diag::Freeze);
            if (c == '2') display::setDiag(display::Diag::Pattern);
            if (c == '3') { wifiOffDiag = !wifiOffDiag; net::setWifiOff(wifiOffDiag); }
            if (c == '4') display::setDiag(display::Diag::Single);
            if (c == '5') display::setTiming((display::timing() + 1) % display::timingCount());
            forceFrame = true;
            printDiag();
        } else if (c == 'R') {
            resetLink("serial");
        } else if (c == 'T') {
            readToken();
        } else if ((c >= 'A' && c <= 'Z') || c == '.') {   // test step letter on screen ('.' clears it)
            diagStep = c == '.' ? 0 : static_cast<char>(c);
            forceFrame = true;
            uprintf("diag: step %c\n", c);
        } else if (c == 'k') {
            net::setFake(!net::fake());
            uprintf("test: fake train data %s\n", net::fake() ? "on" : "off");
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
            uprintf("dock: powering down (reset or replug to wake)\n");
            cta::LedRow off[cta::kLedRows];
            leds::post(off, 0, true);   // night: every LED black
            vTaskDelay(pdMS_TO_TICKS(300));
            display::backlight(false);
            flushSerial();
            esp_deep_sleep_start();
        }
    }
}

}  // namespace

void setup() {
#ifdef PERF_LOG
    uart_driver_install(kUart, 256, 4096, 0, nullptr, 0);
#else
    uart_driver_install(kUart, 256, 0, 0, nullptr, 0);
#endif
    uart_vfs_dev_use_driver(kUart);
    // As Arduino's start-up did: a full or outdated NVS partition is erased and started again; any other error is
    // left alone (erasing would lose the token and the picks).
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK) uprintf("store: NVS didn't start (%d)\n", static_cast<int>(nvs));
    vTaskDelay(pdMS_TO_TICKS(200));
#ifdef PERF_LOG
    perf::begin();
#endif
    uprintf("\ndock: reset reason %d, internal free %u, psram free %u\n", static_cast<int>(esp_reset_reason()),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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
    if (!data || !lists) uprintf("memory: allocation failed\n");
    leds::begin();
    if (!display::begin()) uprintf("display: begin failed\n");
    store::loadPicks(picks);
    printPicks();
    char tok[cta::kTokenLen + 1] = "";
    store::loadToken(tok);
    lan::begin(tok);   // before net::start: the network task starts the link once Wi-Fi is up
    linkCfg.settings = {100, 100, static_cast<uint16_t>(cta::kNightStartMin), static_cast<uint16_t>(cta::kNightEndMin)};
    linkCfg.hasPet = store::loadPet(linkCfg.pet);
    linkCfg.hasSettings = store::loadSettings(linkCfg.settings);
    applyPet();
    applySettings();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    info.dock = true;
    info.battery = -1;
    snprintf(info.id, sizeof(info.id), "%02x%02x", mac[4], mac[5]);
    snprintf(info.fw, sizeof(info.fw), "%s", __DATE__);
    publishLink();
    net::start(picks);
    uprintf("dock: up; internal free %u, psram free %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void loop() {
    static uint32_t lastHalf = UINT32_MAX, lastMinute = UINT32_MAX, lastLeds = 0, statT = 0, frames = 0, sumDraw = 0;
    static uint32_t resetHeldSince = 0;   // when three fingers went down (0 = not held)
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
        const uint32_t m = nowMs();
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
            uprintf("touch: %s %d,%d (%d finger%s)\n", t.kind == display::Touch::Down ? "down" : "up", t.x, t.y,
                          t.fingers, t.fingers == 1 ? "" : "s");
        // The hidden reset: three or more fingers, held without a lift. Such a press is never a tap, hold or drag
        // (the press is dropped), and any report of fewer fingers or a lift restarts the 10 s.
        if (t.kind != display::Touch::Up && t.fingers >= 3) {
            if (!resetHeldSince) resetHeldSince = t.ms | 1;
            app.press.down = false;
            continue;
        }
        resetHeldSince = 0;
        if (t.kind == display::Touch::Down) dock::pointerDown(app, t.x, t.y, t.ms);
        else if (t.kind == display::Touch::Move) dock::pointerMove(app, *lists, t.x, t.y, t.ms);
        else dock::pointerUp(app, *data, *lists, t.x, t.y, t.ms);
        dirty = true;
    }
    const uint32_t ms = nowMs();   // after the touch events, so none is stamped later than this frame
    if (resetHeldSince && static_cast<int32_t>(ms - resetHeldSince) >= static_cast<int32_t>(kResetHoldMs))
        resetLink("three-finger hold");
    dock::tick(app, ms);
    pollSerial();
    // data
    bool dataChanged = false;
    if (net::read(*data, dataVersion)) {
        dirty = dataChanged = true;
        clearStale();   // a snapshot from before the network task took up a new pick still has the old stop
        publishLink();
    }
    // writes from the app (link.cpp answered them already; this only applies them)
    lan::Inbox in;   // on the loop task's stack (already allocated), not more static internal RAM
    if (lan::take(in)) {
        if (in.config && in.cfg.hasStops) {
            memcpy(picks, in.cfg.stops, sizeof(picks));
            setPicks();   // as the on-screen picker does: saved, polled, picks_ver up
            clearStale();
            dataChanged = true;
        }
        if (in.config && in.cfg.hasPet) {
            linkCfg.pet = in.cfg.pet;
            linkCfg.hasPet = true;
            if (!store::savePet(linkCfg.pet)) uprintf("store: saving the pet failed\n");
            applyPet();
        }
        if (in.config && in.cfg.hasSettings) {
            linkCfg.settings = in.cfg.settings;
            linkCfg.hasSettings = true;
            if (!store::saveSettings(linkCfg.settings)) uprintf("store: saving the settings failed\n");
            settingsSaveMs = 0;   // saved just now, a pending tap's value replaced by the app's
            applySettings();
        }
        publishLink();
        if (in.paired) {   // POST /pair: the token is in use already; this keeps it
            if (!store::saveToken(in.token)) uprintf("store: saving the token failed\n");
            uprintf("link: paired\n");
        }
        if (in.reset) resetLink("POST /reset");
        dirty = true;
    }
    if (app.pairAgain) {   // Settings' "Pair again": forget the token, then a fresh code (no reboot)
        app.pairAgain = false;
        store::wipeLink();
        lan::pairAgain();
        uprintf("link: pair again: token forgotten\n");
    }
    // Settings' − / + (ecosystem spec 5.2): applied and in info at once, saved once the taps stop
    if (app.settingsChanged) {
        app.settingsChanged = false;
        linkCfg.settings.screen = app.settings.screen;   // the device's own fields only: an app write this pass
        linkCfg.settings.led = app.settings.led;         // may have brought new quiet hours
        linkCfg.hasSettings = true;
        applySettings();
        publishLink();
        settingsSaveMs = ms ? ms : 1;
        dirty = true;
    }
    if (settingsSaveMs && ms - settingsSaveMs >= kSettingsSaveDelayMs) {
        settingsSaveMs = 0;
        if (!store::saveSettings(linkCfg.settings)) uprintf("store: saving the settings failed\n");
    }
    app.settings = linkCfg.settings;
    {   // pairing: the code until paired, or the cooldown after 5 wrong codes; nothing once paired
        char code[cta::kPairCodeLen + 1] = "";
        uint8_t wait = 0;
        lan::pairShow(code, wait);
        const bool paired = lan::paired();
        if (strcmp(code, app.pairCode) != 0 || wait != app.pairWait || paired != app.paired) {
            memcpy(app.pairCode, code, sizeof(code));
            app.pairWait = wait;
            app.paired = paired;
            dirty = true;
        }
    }
    if (net::readLists(*lists, listsVersion)) {
        dirty = true;
#ifdef PERF_LOG
        const dock::Load st = listWanted == dock::Request::Routes ? lists->routesState
                            : listWanted == dock::Request::Dirs ? lists->dirsState
                            : listWanted == dock::Request::Stops ? lists->stopsState : dock::Load::Loading;
        if (listWanted != dock::Request::None && st != dock::Load::Loading) {
            perf::listLoaded(nowMs() - listAskedMs);
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
        listAskedMs = nowMs();
#endif
        app.request = dock::Request::None;
        dirty = true;
    }
    if (app.ahead.ask) {   // a tapped time's stops ahead, and a tapped card's other way (ecosystem spec 5.10)
        app.ahead.ask = false;
        net::ahead(app.ahead.id, app.ahead.section, app.ahead.vehicle);
    }
    if (app.peek.ask) {
        app.peek.ask = false;
        net::peek(app.peek.id, app.peek.section);
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
    clock.night = clock.now && cta::inNightHours(localMinute(clock.now), linkCfg.settings.nightStart, linkCfg.settings.nightEnd);
    clock.screen = static_cast<uint8_t>(linkCfg.settings.screen);
#ifdef PERF_LOG
    if (stress::running() || net::fake()) clock.night = false;   // runs keep daytime polling and look at any hour
#endif
    net::setSlow(clock.night);
    if (ms / 500 != lastHalf) { lastHalf = ms / 500; dirty = true; }
    if (clock.now / 60 != lastMinute) { lastMinute = clock.now / 60; dirty = true; }
    // the frame
    bool shown = false;   // a frame went to the panel (render waited for it)
    if (dirty || forceFrame || dock::animating(app)) {
        dirty = forceFrame = false;
        const bool profile = profileNext;
        if (profile) {
            profileNext = false;
            memset(ui::gfxCycles, 0, sizeof(ui::gfxCycles));
            ui::gfxClock = cycles;
        }
        const uint32_t t0 = nowMs();
        struct Ctx { const dock::Clock* clock; uint32_t ms; } ctx = {&clock, ms};
        if (display::diag() == display::Diag::Pattern)
            shown = display::render([](ui::Canvas& cv, void*) { drawPattern(cv); }, nullptr);
        else
            shown = display::render([](ui::Canvas& cv, void* p) {
                const Ctx& c = *static_cast<Ctx*>(p);
                dock::draw(cv, app, *data, *lists, *c.clock, c.ms);
                diagLabel(cv);
            }, &ctx);
        sumDraw += nowMs() - t0;
        if (profile) {
            ui::gfxClock = nullptr;
            uprintf("profile: frame %u ms;", static_cast<unsigned>(nowMs() - t0));
            for (int k = 0; k < static_cast<int>(ui::Prim::Count); ++k)
                uprintf(" %s %u", ui::kPrimNames[k], static_cast<unsigned>(ui::gfxCycles[k] / 240000));
            uint32_t du, cu, wu;
            display::lastTimes(du, cu, wu);
            uprintf(" (ms); draw %u, copy %u, panel switch %u ms\n", static_cast<unsigned>(du / 1000),
                          static_cast<unsigned>(cu / 1000), static_cast<unsigned>(wu / 1000));
        }
        ++frames;
#ifdef PERF_LOG
        {
            uint32_t du, cu, wu;
            display::lastTimes(du, cu, wu);
            perf::frame(du, cu, wu, display::lastBands(), dock::animating(app), nowMs(), display::lastBandsDrawn());
        }
#endif
    }
    // the LED rows: their state goes to the LED task (leds.cpp), which pulses LED 7 on its own 25 Hz tick
    if (ms - lastLeds >= 200 || dataChanged) {
        lastLeds = ms;
        cta::LedRow rows[cta::kLedRows];
        for (int i = 0; i < cta::kLedRows; ++i) {
            const dock::Section& s = data->sec[i];
            if (!s.pick.id[0]) continue;
            const bool peek = dock::peekShown(app, *data, i);   // the row follows the card's other way (spec 6.4)
            rows[i].mask = peek ? s.peekMask : s.mask;
            rows[i].rgb = s.pick.type == cta::Kind::Bus ? cta::kLedBusColor : cta::ledLineColor(s.pick.route);
            rows[i].ts = s.posTs;
            rows[i].due = cta::atStop(peek ? data->peek.rec : s.rec, clock.now);
            rows[i].level = linkCfg.settings.led > 100 ? 100 : linkCfg.settings.led;   // GET /leds allows 0-100
        }
        leds::post(rows, clock.now, clock.night);
        if (printLeds) {
            printLeds = false;
            uint32_t rgb[cta::kLedCount];
            cta::ledFrame(rows, clock.now, clock.night, LED_MAX_BRIGHTNESS, ms, rgb);
            for (int r = 0; r < cta::kLedRows; ++r) {
                uprintf("leds row %d (LED 1..7):", r + 1);
                for (int i = 0; i < cta::kLedsPerRow; ++i) uprintf(" %06X", static_cast<unsigned>(rgb[cta::ledIndex(r, i)]));
                uprintf("\n");
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
        if (late) uprintf("scan: at %u ms, %u frames, %u late, longest %u us (period %u us)\n", ms, f, late, gap, period);
    }
    if (ms - statT >= 30000) {
        statT = ms;
        uprintf("stats: %u frames, %u ms each, %u switch timeouts, %u DMA fallbacks; internal free %u (min %u), psram free %u; %u CTA requests; httpd stack left %u\n",
                      frames, frames ? sumDraw / frames : 0, display::timeouts(), static_cast<unsigned>(display::dmaFallbacks()),
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                      heap_caps_get_free_size(MALLOC_CAP_SPIRAM), net::requests(), static_cast<unsigned>(lan::stackLeft()));
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
    // A pass that showed a frame has already slept on core 1 while the panel took it, and a drag's next frame should
    // start now: frames move in panel steps (29.3 ms), and 2 ms more before each could push one past the next step
    // (dock performance analysis, round 3). A pass without one sleeps, so the loop never spins.
    if (!shown) vTaskDelay(pdMS_TO_TICKS(2));
}

// ESP-IDF's main task, on core 1 with an 8 KB stack (sdkconfig.defaults), as Arduino's loop task was.
extern "C" void app_main() {
    setup();
    for (;;) loop();
}
