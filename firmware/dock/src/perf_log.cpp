#include "perf_log.h"

#ifdef PERF_LOG

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "display.h"

namespace perf {

namespace {

// The columns of a `P,` line, in order. tools/dock_perf_report.py reads them by name from the `H,` line, so new ones
// go at the end. Times this second: draw/copy/wait are summed over the second's frames (divide by frames for a
// frame's); tap and drag are averages over the second's taps and drag frames, -max the worst.
constexpr const char* kColumns =
    "ms,phase,screen,cpu_c,busy0,busy1,frames,bands,draw_ms,copy_ms,wait_ms,move_fps,slow_frame_ms,tap_ms,tap_max_ms,"
    "drag_ms,drag_max_ms,touches,list_ms,req_started,req_done,req_ms,req_bytes,wifi_status,rssi,scan_late,recoveries,"
    "switch_timeouts,int_free,int_min,psram_free,sections,picked,data_age,log_us,drawn";

// ---------- the ring: whole lines, PSRAM ----------

constexpr size_t kRing = 256 * 1024;
char* ring = nullptr;
size_t head = 0, tail = 0;   // write at head, send from tail; empty when equal
uint32_t dropped = 0;
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

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;   // what the network task (core 0) touches

// Network task.
uint32_t reqStarted = 0, reqDone = 0, reqBytes = 0;
int64_t reqActiveUs = 0, reqSince = 0;
volatile bool burst = false;
uint32_t lastBurstMs = 0;
constexpr uint32_t kBurstEveryMs = 2000, kBurstMax = 180;
uint32_t burstCount = 0;

// Frame loop.
uint32_t frames = 0, bands = 0, touches = 0, scanLate = 0;
uint32_t drawn = 0;   // bands drawn (the rest the hidden buffer already held)
uint64_t drawUs = 0, copyUs = 0, waitUs = 0;
uint32_t moveIntervals = 0, moveMs = 0, slowFrameMs = 0, lastFrameMs = 0;
bool lastMoving = false;
uint32_t pendingTap = 0, pendingMove = 0;   // touch times waiting for the next frame (0: none)
bool havePendingTap = false, havePendingMove = false;
uint32_t tapSum = 0, tapN = 0, tapMax = 0, dragSum = 0, dragN = 0, dragMax = 0;
int32_t listMs = -1;

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
    if (!ring) Serial.println("perf log: no memory for the ring");
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
        const float b = wallUs > 0 ? 100.f * (1.f - static_cast<float>(d) / static_cast<float>(wallUs)) : 0;
        busy[c] = b < 0 ? 0 : b > 100 ? 100 : b;
    }

    portENTER_CRITICAL(&lock);
    const uint32_t rs = reqStarted, rd = reqDone, rb = reqBytes;
    if (reqSince) { reqActiveUs += t0 - reqSince; reqSince = t0; }
    const int64_t ra = reqActiveUs;
    reqStarted = reqDone = reqBytes = 0;
    reqActiveUs = 0;
    portEXIT_CRITICAL(&lock);

    const int status = static_cast<int>(WiFi.status());
    const int rssi = status == WL_CONNECTED ? WiFi.RSSI() : 0;

    char fps[12] = "", slow[12] = "", tap[12] = "", tapM[12] = "", drag[12] = "", dragM[12] = "", list[12] = "",
         age[12] = "";
    if (moveIntervals && moveMs) snprintf(fps, sizeof(fps), "%.1f", moveIntervals * 1000.f / moveMs);
    if (moveIntervals) snprintf(slow, sizeof(slow), "%u", static_cast<unsigned>(slowFrameMs));
    if (tapN) {
        snprintf(tap, sizeof(tap), "%u", static_cast<unsigned>(tapSum / tapN));
        snprintf(tapM, sizeof(tapM), "%u", static_cast<unsigned>(tapMax));
    }
    if (dragN) {
        snprintf(drag, sizeof(drag), "%.1f", static_cast<float>(dragSum) / dragN);
        snprintf(dragM, sizeof(dragM), "%u", static_cast<unsigned>(dragMax));
    }
    if (listMs >= 0) snprintf(list, sizeof(list), "%d", static_cast<int>(listMs));
    if (in.dataAgeSec >= 0) snprintf(age, sizeof(age), "%d", in.dataAgeSec);

    static uint32_t lastLogUs = 0;   // the logger's own time, reported one line late
    char line[512];
    const int n = snprintf(
        line, sizeof(line),
        "P,%u,%u,%u,%.1f,%.1f,%.1f,%u,%u,%.1f,%.1f,%.1f,%s,%s,%s,%s,%s,%s,%u,%s,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%d,%d,%s,%u,%u\n",
        static_cast<unsigned>(ms), in.phase, in.screen, temperatureRead(), busy[0], busy[1],
        static_cast<unsigned>(frames), static_cast<unsigned>(bands), drawUs / 1000.0, copyUs / 1000.0, waitUs / 1000.0,
        fps, slow, tap, tapM, drag, dragM, static_cast<unsigned>(touches), list, static_cast<unsigned>(rs),
        static_cast<unsigned>(rd), static_cast<unsigned>(ra / 1000), static_cast<unsigned>(rb), status, rssi,
        static_cast<unsigned>(scanLate), static_cast<unsigned>(display::recoveries()),
        static_cast<unsigned>(display::timeouts()), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), in.sections, in.picked, age,
        static_cast<unsigned>(lastLogUs), static_cast<unsigned>(drawn));
    append(line, static_cast<size_t>(n));
    if (dropped) { event("dropped,%u", static_cast<unsigned>(dropped)); dropped = 0; }

    frames = bands = touches = scanLate = drawn = 0;
    drawUs = copyUs = waitUs = 0;
    moveIntervals = moveMs = slowFrameMs = 0;
    tapSum = tapN = tapMax = dragSum = dragN = dragMax = 0;
    listMs = -1;
    lastLogUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
}

void flush() {
    if (!ring) return;
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
}

void touch() { ++touches; }

void tapUp(uint32_t touchMs) {
    if (!havePendingTap) { pendingTap = touchMs; havePendingTap = true; }
}

void move(uint32_t touchMs) {
    if (!havePendingMove) { pendingMove = touchMs; havePendingMove = true; }   // the oldest unshown sample
}

void frame(uint32_t dUs, uint32_t cUs, uint32_t wUs, int bandsCopied, bool moving, uint32_t doneMs, int bandsDrawn) {
    ++frames;
    bands += static_cast<uint32_t>(bandsCopied);
    drawn += static_cast<uint32_t>(bandsDrawn);
    drawUs += dUs;
    copyUs += cUs;
    waitUs += wUs;
    if (moving && lastMoving) {
        const uint32_t gap = doneMs - lastFrameMs;
        ++moveIntervals;
        moveMs += gap;
        if (gap > slowFrameMs) slowFrameMs = gap;
    }
    lastMoving = moving;
    lastFrameMs = doneMs;
    if (havePendingTap) {
        const uint32_t d = doneMs - pendingTap;
        tapSum += d; ++tapN;
        if (d > tapMax) tapMax = d;
        havePendingTap = false;
        event("tap,%u", static_cast<unsigned>(d));
    }
    if (havePendingMove) {
        const uint32_t d = doneMs - pendingMove;
        dragSum += d; ++dragN;
        if (d > dragMax) dragMax = d;
        havePendingMove = false;
    }
}

void listLoaded(uint32_t ms) {
    listMs = static_cast<int32_t>(ms);
    event("list,%u", static_cast<unsigned>(ms));
}

void scan(uint32_t late) { scanLate += late; }

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

void setBurst(bool on) {
    burst = on;
    lastBurstMs = millis();
    if (!on) burstCount = 0;   // the cap is per burst phase (60 + 90 a run at most)
}

bool burstDue() {
    if (!burst || burstCount >= kBurstMax) return false;
    const uint32_t ms = millis();
    if (ms - lastBurstMs < kBurstEveryMs) return false;
    lastBurstMs = ms;
    ++burstCount;
    return true;
}

}  // namespace perf

#endif  // PERF_LOG
