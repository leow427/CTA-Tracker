#pragma once
// The handheld's screens and gestures, ported from docs/ideas/2026-11-04-handheld-ui-mockup.html and drawn from
// mock::Model. Plain C++: the device feeds it touch points and pushes the framebuffer; tools/preview.cpp renders
// it on a laptop.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gfx2d.h"
#include "mock_data.h"

namespace ui {

constexpr int W = 466, C = 233;   // the round screen: its width and height, and its center

enum class Screen : uint8_t { Home, Stops, Detail, Map, Pair, First };
constexpr int kRow = 112;   // the Stops list's card pitch: a list at rest sits on a multiple of it
enum class Tab : uint8_t { Trains = 0, Buses = 1 };

struct View {
    Screen screen = Screen::Home;
    Tab tab = Tab::Trains;
    // The detail screen's card, by identity, since the list re-sorts as arrivals change.
    uint8_t station = 0;
    char route[8] = "";
    char dir[26] = "";
    Screen back = Screen::Stops;
};

struct App {
    View view;
    bool dim = false;
    float scroll[2] = {0, 0}, scrollTarget[2] = {0, 0};
    float detailScroll = 0, detailTarget = 0;
    // How long each scroll takes to settle (ms, exponential): short after a drag, longer after a fling so the list
    // leaves the finger at the finger's speed.
    float scrollTau[2] = {51, 51}, detailTau = 51;
    int detailH = 0;
    struct { bool on = false; View from; bool xAxis = false; int dir = 1; uint32_t t0 = 0; } trans;
    uint32_t pokeT0 = 0;
    bool poked = false;
    uint32_t lastInput = 0;
    struct {
        bool down = false, moved = false, isLong = false, wasDim = false;
        bool caught = false;   // the press stopped a moving list, so lifting it isn't a tap
        float x0 = 0, y0 = 0, x = 0, y = 0, s0 = 0, mx0 = 0, my0 = 0;
        uint32_t t0 = 0;
        // The last few touch samples, for the release velocity.
        struct Sample { float x, y; uint32_t t; } hist[8];
        uint32_t nHist = 0;   // samples seen this press; hist is a ring of the last 8
    } drag;
    float mapX = 0, mapY = 0;
    bool mapFollow = true;
    char toast[40] = "";
    char gesture[128] = "";   // what the last release did, for the serial gesture log; the reader clears it
    uint32_t toastT0 = 0;
    uint32_t lastTick = 0;
};

void init(App& a, uint32_t ms);
// Optional: memory (cardCacheBytes(entries) bytes, 4-byte aligned) for `entries` rendered cards and their keys, so
// the list redraws them only on change.
size_t cardCacheBytes(int entries);
void setCardCache(void* mem, int entries);
uint32_t cardRenders();   // cards rendered so far (into the cache or scratch), for tools/frame_test.cpp
// Easing, auto-dim, and the long-press debug gesture. Call once per frame.
void tick(App& a, mock::Model& m, uint32_t ms);
// Draws the whole frame. scratch must hold kScratchPixels.
constexpr int kScratchPixels = 340 * 102;
void draw(Canvas& cv, App& a, const mock::Model& m, uint32_t ms, uint16_t* scratch);
// 0–1: the panel brightness for the current state (dim, docked).
float brightness(const App& a, const mock::Model& m);
// True while anything moves faster than the 2 Hz bob: a transition, a drag or scroll settling, a hop, rain or snow.
bool animating(const App& a, const mock::Model& m, uint32_t ms);
// The Stops list at rest (nothing moving, no transition, no toast): a key of everything its frame draws except the
// live marks' blink, and the marks' screen rectangles. Two such frames with equal keys differ only inside those
// rectangles, so the device redraws just them (main.cpp). 0 for any other frame. A new input to that screen must be
// added here.
struct Mark { int16_t x, y, w, h; };
uint32_t restKey(const App& a, const mock::Model& m, uint32_t ms, Mark* marks, int maxMarks, int& nMarks);
// True on the dim always-on screen (Home or First dimmed: the status row and Trackie alone), not mid-transition.
bool alwaysOn(const App& a);
// Everything the always-on screen's frame depends on (what drawHome reads when dimmed, the burn-in shift, and the
// brightness). While alwaysOn(), two frames with equal keys draw the same pixels at the same brightness, so the device
// skips the second (main.cpp). A new input to that screen must be added here.
struct AodKey {
    uint8_t screen, mood;
    bool dim, docked, clockKnown, stale;
    int8_t ox, oy, bob;
    char clock[8];
    char temp[8];
    const char* icon;
    uint32_t rgb;
    bool operator==(const AodKey& o) const {
        return screen == o.screen && mood == o.mood && dim == o.dim && docked == o.docked && clockKnown == o.clockKnown &&
               stale == o.stale && ox == o.ox && oy == o.oy && bob == o.bob && strcmp(clock, o.clock) == 0 &&
               strcmp(temp, o.temp) == 0 && icon == o.icon && rgb == o.rgb;
    }
    bool operator!=(const AodKey& o) const { return !(*this == o); }
};
AodKey aodKey(const App& a, const mock::Model& m, uint32_t ms);

// Touch, in screen coordinates, with each sample's own time (velocity comes from those times, not frame times).
void pointerDown(App& a, mock::Model& m, float x, float y, uint32_t ms);
void pointerMove(App& a, mock::Model& m, float x, float y, uint32_t ms);
void pointerUp(App& a, mock::Model& m, float x, float y, uint32_t ms);
void cycleScenario(App& a, mock::Model& m, uint32_t ms);   // the debug gesture
void cycleWeather(App& a, mock::Model& m, uint32_t ms);

}  // namespace ui
