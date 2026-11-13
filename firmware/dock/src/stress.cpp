#include "stress.h"

#ifdef PERF_LOG

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "perf_log.h"
#include "approach.h"

namespace stress {

namespace {

// ---------- the phases (spec section 3) ----------
enum class Mode : uint8_t { Idle, Churn, Map, Scroll, Poke, Mixed };
struct Phase { const char* name; uint32_t ms; Mode mode; bool burst; };
const Phase kPhases[] = {
    {"warmup", 180000, Mode::Idle, false},     // 0: until ready, at most 3 min (not scored)
    {"idle", 120000, Mode::Idle, false},       // 1
    {"churn", 120000, Mode::Churn, false},     // 2
    {"map", 120000, Mode::Map, false},         // 3
    {"scroll", 120000, Mode::Scroll, false},   // 4
    {"burst", 120000, Mode::Poke, true},       // 5
    {"mixed", 180000, Mode::Mixed, true},      // 6
    {"cooldown", 120000, Mode::Idle, false},   // 7
};
constexpr int kPhaseCount = sizeof(kPhases) / sizeof(kPhases[0]);

// Where things are on the portrait screen (dock_ui.cpp's layout constants).
constexpr int16_t kTrackie[2] = {40, 40}, kMapBtn[2] = {328, 38}, kSection1[2] = {240, 198};
constexpr int16_t kTrain[2] = {240, 245}, kBus[2] = {240, 525}, kRed[2] = {128, 174}, kDir0[2] = {240, 232};
constexpr int16_t kBack[2] = {35, 42};

// ---------- the touch queue ----------
struct Ev { uint32_t at; display::Touch::Kind kind; int16_t x, y; };
constexpr int kQ = 512;
Ev q[kQ];
int qHead = 0, qTail = 0;   // pop at head, push at tail
uint32_t cursor = 0;        // when the next pushed event is due
bool fingerDown = false;    // the last event handed out was a down or a move
int16_t lastX = 0, lastY = 0;

bool qEmpty() { return qHead == qTail; }
void push(uint32_t dt, display::Touch::Kind k, int x, int y) {
    cursor += dt;
    const int next = (qTail + 1) % kQ;
    if (next == qHead) return;   // full: the generators never queue more than one gesture
    q[qTail] = {cursor, k, static_cast<int16_t>(x), static_cast<int16_t>(y)};
    qTail = next;
}
void wait(uint32_t ms) { cursor += ms; }
void tap(const int16_t* p) {
    push(0, display::Touch::Down, p[0], p[1]);
    push(60, display::Touch::Up, p[0], p[1]);
}
void longPress(const int16_t* p) {
    push(0, display::Touch::Down, p[0], p[1]);
    push(800, display::Touch::Up, p[0], p[1]);
}
// A drag along f(u), u from 0 to 1, over ms, with a sample every 10 ms (the touch task's rate).
template <typename F>
void drag(uint32_t ms, F f) {
    int x, y;
    f(0.f, x, y);
    push(0, display::Touch::Down, x, y);
    for (uint32_t t = 10; t <= ms; t += 10) {
        f(static_cast<float>(t) / ms, x, y);
        push(10, display::Touch::Move, x, y);
    }
    push(10, display::Touch::Up, x, y);
}

// ---------- the gestures each mode repeats ----------
// One loop of screen churn: Trackie, the map and back, the picker's train steps and back, its bus routes and back.
int churnStep = 0;
constexpr int kChurnSteps = 13;
void churnNext() {
    switch (churnStep) {
        case 0: tap(kTrackie); break;
        case 1: tap(kMapBtn); break;         // the map
        case 2: tap(kMapBtn); break;         // the stops
        case 3: longPress(kSection1); break; // the picker, step 1
        case 4: tap(kTrain); break;          // step 2: the lines
        case 5: tap(kRed); break;            // step 3: the directions
        case 6: tap(kDir0); break;           // step 4: the stations
        case 7: case 8: case 9: tap(kBack); break;   // back to step 1
        case 10: tap(kBus); wait(1800); break;       // the bus routes, fetched live (list_ms)
        case 11: case 12: tap(kBack); break;          // step 1, then the stops
    }
    wait(1200);
    churnStep = (churnStep + 1) % kChurnSteps;
}
void mapNext() {   // one circle around the map's middle in 2 s: the pan comes back to where it started
    drag(2000, [](float u, int& x, int& y) {
        x = 240 + static_cast<int>(lroundf(120 * cosf(6.2831853f * u)));
        y = 450 + static_cast<int>(lroundf(120 * sinf(6.2831853f * u)));
    });
    wait(300);
}
bool scrollUp = true;
void scrollNext() {   // 400 px up the station list in 1 s, then back down
    const bool up = scrollUp;
    drag(1000, [up](float u, int& x, int& y) {
        x = 240;
        y = static_cast<int>(lroundf(up ? 600 - 400 * u : 200 + 400 * u));
    });
    wait(200);
    scrollUp = !scrollUp;
}

// ---------- the script's state ----------
bool on = false;
int ph = 0;
uint32_t phaseStart = 0;
Mode segMode = Mode::Idle;   // in the mixed phase, the segment running
int segCount = 0;            // gestures (or churn steps) done in this segment

void home(dock::App& a) {
    a.screen = dock::Screen::Home;
    a.picker.step = 1;
    a.picker.scroll = 0;
    a.panX = a.panY = 0;
}
void setUp(dock::App& a, Mode m) {
    home(a);
    churnStep = 0;
    scrollUp = true;
    if (m == Mode::Map) a.screen = dock::Screen::Map;
    if (m == Mode::Scroll) {   // the Red Line's stations, first direction: the longest train list
        a.screen = dock::Screen::Picker;
        a.picker.section = 0;
        a.picker.bus = false;
        snprintf(a.picker.line, sizeof(a.picker.line), "Red");
        a.picker.lineName[0] = 0;
        const char* dirs[4];
        cta::trainDirections("Red", dirs, 4);
        snprintf(a.picker.dirName, sizeof(a.picker.dirName), "%s", dirs[0]);
        a.picker.dir = 0;
        a.picker.step = 4;
    }
}

void flushQueue(uint32_t ms) {
    qHead = qTail = 0;
    cursor = ms;
    if (fingerDown) push(0, display::Touch::Up, lastX, lastY);   // never leave a finger down across a phase
}

void enter(dock::App& a, int p, uint32_t ms) {
    ph = p;
    phaseStart = ms;
    flushQueue(ms);
    const Phase& P = kPhases[p];
    perf::setBurst(false);
    if (P.burst) perf::setBurst(true);
    segMode = P.mode == Mode::Mixed ? Mode::Map : P.mode;
    segCount = 0;
    setUp(a, segMode);
    perf::event("phase,%d", p);
}

// The next gesture of the current mode, once the queue has run dry.
void refill(dock::App& a, uint32_t ms) {
    if (cursor < ms) cursor = ms;
    if (kPhases[ph].mode == Mode::Mixed) {
        // 5 map circles, one churn loop, 5 scrolls up and down, around again
        const bool segDone = (segMode == Mode::Map && segCount >= 5) || (segMode == Mode::Churn && segCount >= kChurnSteps) ||
                             (segMode == Mode::Scroll && segCount >= 10);
        if (segDone) {
            segMode = segMode == Mode::Map ? Mode::Churn : segMode == Mode::Churn ? Mode::Scroll : Mode::Map;
            segCount = 0;
            setUp(a, segMode);
        }
    }
    switch (segMode) {
        case Mode::Idle: return;
        case Mode::Churn: churnNext(); break;
        case Mode::Map: mapNext(); break;
        case Mode::Scroll: scrollNext(); break;
        case Mode::Poke: tap(kTrackie); wait(2940); break;   // a tap on Trackie every 3 s
        case Mode::Mixed: return;
    }
    ++segCount;
}

}  // namespace

void start() {
    on = true;
    ph = -1;
    perf::header();
    perf::event("start,%s", kVersion);
}

void stop() {
    if (!on) return;
    on = false;
    perf::setBurst(false);
    qHead = qTail = 0;
    perf::event("stopped");
}

bool running() { return on; }
uint8_t phase() { return on && ph > 0 ? static_cast<uint8_t>(ph) : 0; }

void tick(dock::App& a, uint32_t ms, bool ready) {
    if (!on) return;
    if (ph < 0) { enter(a, 0, ms); return; }
    const uint32_t in = ms - phaseStart;
    if ((ph == 0 && ready) || in >= kPhases[ph].ms) {
        if (ph == 0) perf::event("warmup_done,%s,%u", ready ? "ready" : "timeout", static_cast<unsigned>(in));
        if (ph + 1 >= kPhaseCount) {
            on = false;
            perf::setBurst(false);
            qHead = qTail = 0;
            fingerDown = false;
            a.press.down = false;
            home(a);
            perf::event("done");
            return;
        }
        enter(a, ph + 1, ms);
        return;
    }
    if (qEmpty() && !fingerDown) refill(a, ms);
}

bool nextTouch(display::Touch& t) {
    if (qEmpty() || static_cast<int32_t>(millis() - q[qHead].at) < 0) return false;
    const Ev& e = q[qHead];
    qHead = (qHead + 1) % kQ;
    t.kind = e.kind;
    t.fingers = e.kind == display::Touch::Up ? 0 : 1;
    t.x = e.x;
    t.y = e.y;
    t.ms = millis();
    fingerDown = e.kind != display::Touch::Up;
    lastX = e.x;
    lastY = e.y;
    return true;
}

}  // namespace stress

#endif  // PERF_LOG
