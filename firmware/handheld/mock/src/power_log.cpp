#include "power_log.h"

#ifdef POWER_LOG

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace plog {

namespace {

// The columns of a `P,` line, in order. tools/power_report.py reads them by name from the `H,` line.
constexpr const char* kColumns =
    "ms,phase,usb_mv,sys_mv,pmu_c,cpu_c,batt,batt_pct,batt_mv,cpu_mhz,busy0,busy1,dim,bright,frames,px_sent,lit,"
    "wifi_mode,wifi_status,rssi,tx_qdbm,wifi_ps,req_started,req_done,req_ms,req_bytes,ble,ble_interval,touches,"
    "move_fps,slow_frame_ms,tap_ms,data_age,int_free,int_min,psram_free,trains,buses,log_us";

// ---------- the ring: whole lines, PSRAM, at least 10 minutes of samples ----------

constexpr size_t kRing = 256 * 1024;
char* ring = nullptr;
size_t head = 0, tail = 0;   // write at head, send from tail; empty when equal
uint32_t dropped = 0;        // lines that didn't fit
portMUX_TYPE ringLock = portMUX_INITIALIZER_UNLOCKED;

size_t used() { return head >= tail ? head - tail : kRing - tail + head; }

void append(const char* s, size_t n) {
    if (!ring) return;
    portENTER_CRITICAL(&ringLock);
    if (n + 1 >= kRing - used()) {
        ++dropped;
    } else {
        for (size_t i = 0; i < n; ++i) { ring[head] = s[i]; head = (head + 1) % kRing; }
    }
    portEXIT_CRITICAL(&ringLock);
}

// ---------- counters ----------

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;   // for everything core 0 touches

// Panel sender (core 0).
uint32_t pxSent = 0;
uint16_t tapSeq = 0;
bool tapArmed = false;      // frame tapSeq carries a tap
int64_t tapUs = 0;
int32_t tapMs = -1;         // the latest measured, this second

// Network task (core 0).
uint32_t reqStarted = 0, reqDone = 0, reqBytes = 0;
int64_t reqActiveUs = 0;    // time with a request in progress, this second
int64_t reqSince = 0;       // a request is in progress since then (0: none)

// Frame loop (core 1).
uint32_t frames = 0, touches = 0;
uint32_t moveIntervals = 0, moveMs = 0, slowFrameMs = 0;
uint32_t lastFrameMs = 0;
bool lastMoving = false;
int64_t pendingTapUs = 0;   // a tap waiting for the next frame
bool litNow = false, litFresh = false;
uint64_t litSum = 0, litN = 0;
float lit = -1;

uint32_t nextSampleMs = 0;
int64_t lastSampleUs = 0;
uint32_t lastIdle[2] = {0, 0};

}  // namespace

void header() {
    char line[512];
    const int n = snprintf(line, sizeof(line), "H,%s\n", kColumns);
    append(line, static_cast<size_t>(n));
}

void begin() {
    ring = static_cast<char*>(heap_caps_malloc(kRing, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!ring) Serial.println("power log: no memory for the ring");
    lastSampleUs = esp_timer_get_time();
    for (int c = 0; c < 2; ++c) lastIdle[c] = ulTaskGetIdleRunTimeCounterForCore(c);
    temperatureRead();   // installs the sensor now, not mid-run
    event("boot,reset_reason=%d", static_cast<int>(esp_reset_reason()));
    header();
}

bool due(uint32_t ms) { return static_cast<int32_t>(ms - nextSampleMs) >= 0; }

void event(const char* fmt, ...) {
    char line[256];
    int n = snprintf(line, sizeof(line), "E,%u,", static_cast<unsigned>(millis()));
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
    va_end(ap);
    if (n > static_cast<int>(sizeof(line)) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    append(line, static_cast<size_t>(n));
}

void sample(uint32_t ms, const Inputs& in) {
    const int64_t t0 = esp_timer_get_time();
    nextSampleMs = (nextSampleMs ? nextSampleMs : ms) + 1000;
    if (static_cast<int32_t>(ms - nextSampleMs) >= 0) nextSampleMs = ms + 1000;   // fell behind: don't burst

    // CPU: each core's idle task run time against the wall clock (FreeRTOS run-time stats, esp_timer µs, 32-bit).
    const int64_t wallUs = t0 - lastSampleUs;
    lastSampleUs = t0;
    float busy[2];
    for (int c = 0; c < 2; ++c) {
        const uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(c);
        const uint32_t d = idle - lastIdle[c];   // unsigned: survives the 71-minute wrap
        lastIdle[c] = idle;
        float b = wallUs > 0 ? 100.f * (1.f - static_cast<float>(d) / static_cast<float>(wallUs)) : 0;
        busy[c] = b < 0 ? 0 : b > 100 ? 100 : b;
    }

    // Core 0's counters.
    portENTER_CRITICAL(&lock);
    const uint32_t px = pxSent, rs = reqStarted, rd = reqDone, rb = reqBytes;
    if (reqSince) { reqActiveUs += t0 - reqSince; reqSince = t0; }
    const int64_t ra = reqActiveUs;
    const int32_t tm = tapMs;
    pxSent = reqStarted = reqDone = reqBytes = 0;
    reqActiveUs = 0;
    tapMs = -1;
    portEXIT_CRITICAL(&lock);

    // Wi-Fi.
    const wifi_mode_t mode = WiFi.getMode();
    int status = -1, rssi = 0, txq = 0, ps = -1;
    if (mode != WIFI_MODE_NULL) {
        status = static_cast<int>(WiFi.status());
        if (status == WL_CONNECTED) rssi = WiFi.RSSI();
        int8_t q = 0;
        if (esp_wifi_get_max_tx_power(&q) == ESP_OK) txq = q;
        wifi_ps_type_t p;
        if (esp_wifi_get_ps(&p) == ESP_OK) ps = static_cast<int>(p);
    }

    char fps[12] = "", slow[12] = "", tap[12] = "", litS[12] = "", age[12] = "";
    if (moveIntervals && moveMs) snprintf(fps, sizeof(fps), "%.1f", moveIntervals * 1000.f / moveMs);
    if (moveIntervals) snprintf(slow, sizeof(slow), "%u", static_cast<unsigned>(slowFrameMs));
    if (tm >= 0) snprintf(tap, sizeof(tap), "%d", static_cast<int>(tm));
    if (lit >= 0) snprintf(litS, sizeof(litS), "%.4f", lit);
    if (in.dataAgeSec >= 0) snprintf(age, sizeof(age), "%d", in.dataAgeSec);

    static uint32_t lastLogUs = 0;   // the logger's own time, reported one line late
    char line[512];
    const int n = snprintf(
        line, sizeof(line),
        "P,%u,%u,%u,%u,%.1f,%.1f,%d,%d,%u,%u,%.1f,%.1f,%d,%u,%u,%u,%s,%d,%d,%d,%d,%d,%u,%u,%u,%u,,,%u,%s,%s,%s,%s,%u,%u,%u,%d,%d,%u\n",
        static_cast<unsigned>(ms), in.phase, in.usbMv, in.sysMv, in.pmuTempC, temperatureRead(), in.battery, in.battPct,
        in.battMv, static_cast<unsigned>(getCpuFrequencyMhz()), busy[0], busy[1], in.dim, in.brightness,
        static_cast<unsigned>(frames), static_cast<unsigned>(px), litS, static_cast<int>(mode), status, rssi, txq, ps,
        static_cast<unsigned>(rs), static_cast<unsigned>(rd), static_cast<unsigned>(ra / 1000), static_cast<unsigned>(rb),
        static_cast<unsigned>(touches), fps, slow, tap, age,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), in.trainCards, in.busCards,
        static_cast<unsigned>(lastLogUs));
    append(line, static_cast<size_t>(n));
    if (dropped) { event("dropped,%u", static_cast<unsigned>(dropped)); dropped = 0; }

    frames = touches = moveIntervals = moveMs = slowFrameMs = 0;
    if (!litFresh) lit = -1;   // no frame drawn this second: nothing new on screen
    litFresh = false;
    lastLogUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
}

void flush() {
    if (!ring || !Serial) return;   // nothing to send, or no USB host: the ring holds it
    const int64_t t0 = esp_timer_get_time();
    static char line[512];
    for (int k = 0; k < 16; ++k) {
        size_t n = 0;
        portENTER_CRITICAL(&ringLock);
        for (size_t i = tail; i != head && n < sizeof(line); i = (i + 1) % kRing) {
            line[n++] = ring[i];
            if (ring[i] == '\n') break;
        }
        portEXIT_CRITICAL(&ringLock);
        if (n == 0 || line[n - 1] != '\n') break;   // empty
        if (Serial.availableForWrite() < static_cast<int>(n)) break;
        Serial.write(reinterpret_cast<const uint8_t*>(line), n);
        portENTER_CRITICAL(&ringLock);
        tail = (tail + n) % kRing;
        portEXIT_CRITICAL(&ringLock);
    }
    (void)t0;
}

void frame(bool moving, uint32_t ms) {
    ++frames;
    if (moving && lastMoving) {
        const uint32_t gap = ms - lastFrameMs;
        ++moveIntervals;
        moveMs += gap;
        if (gap > slowFrameMs) slowFrameMs = gap;
    }
    lastMoving = moving;
    lastFrameMs = ms;
}

bool litWanted() {
    litNow = !litFresh;   // one frame a second
    if (litNow) { litSum = 0; litN = 0; }
    return litNow;
}

void litStrip(const uint16_t* px, int n) {
    if (!litNow) return;
    // Every 16th pixel of the strip: with 48-row strips that is every 16th screen row, every column. Each channel
    // counts in proportion to its level (black draws nearly nothing on an AMOLED).
    uint32_t sum = 0, cnt = 0;
    for (int i = 0; i < n; i += 16) {
        const uint16_t c = px[i];
        const uint16_t v = static_cast<uint16_t>((c << 8) | (c >> 8));
        sum += ((v >> 11) & 31) * 2 + ((v >> 5) & 63) + (v & 31) * 2;   // each channel out of 62/63
        ++cnt;
    }
    litSum += sum;
    litN += cnt;
}

void litDone() {
    if (!litNow) return;
    litNow = false;
    if (litN) { lit = static_cast<float>(litSum) / (litN * 187.f); litFresh = true; }
}

void tap() {
    if (!pendingTapUs) pendingTapUs = esp_timer_get_time();
}

void frameQueued(uint16_t seq) {
    if (!pendingTapUs) return;
    portENTER_CRITICAL(&lock);
    tapSeq = seq;
    tapArmed = true;
    tapUs = pendingTapUs;
    portEXIT_CRITICAL(&lock);
    pendingTapUs = 0;
}

void touch() { ++touches; }

void sent(uint32_t px) {
    portENTER_CRITICAL(&lock);
    pxSent += px;
    portEXIT_CRITICAL(&lock);
}

void frameDone(uint16_t seq) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    if (tapArmed && seq == tapSeq) {
        tapArmed = false;
        tapMs = static_cast<int32_t>((now - tapUs) / 1000);
    }
    portEXIT_CRITICAL(&lock);
}

void reqStart(char kind) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    ++reqStarted;
    if (!reqSince) reqSince = now;
    portEXIT_CRITICAL(&lock);
    event("req_start,%c", kind);
}

void reqEnd(char kind, int code, uint32_t bytes, uint32_t ms) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    ++reqDone;
    reqBytes += bytes;
    if (reqSince) { reqActiveUs += now - reqSince; reqSince = 0; }
    portEXIT_CRITICAL(&lock);
    event("req_end,%c,%d,%u,%u", kind, code, static_cast<unsigned>(bytes), static_cast<unsigned>(ms));
}

}  // namespace plog

#endif  // POWER_LOG
