#pragma once
// The handheld's screens and gestures, ported from docs/ideas/2026-11-04-handheld-ui-mockup.html and drawn from
// mock::Model. Plain C++: the device feeds it touch points and pushes the framebuffer; tools/preview.cpp renders
// it on a laptop.
#include <stdint.h>

#include "gfx2d.h"
#include "mock_data.h"

namespace ui {

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
// Optional: memory for `entries` rendered cards (kScratchPixels each), so the list redraws them only on change.
void setCardCache(uint16_t* mem, int entries);
// Easing, auto-dim, and the long-press debug gesture. Call once per frame.
void tick(App& a, mock::Model& m, uint32_t ms);
// Draws the whole frame. scratch must hold kScratchPixels.
constexpr int kScratchPixels = 340 * 102;
void draw(Canvas& cv, App& a, const mock::Model& m, uint32_t ms, uint16_t* scratch);
// 0–1: the panel brightness for the current state (dim, docked).
float brightness(const App& a, const mock::Model& m);
// True while anything moves faster than the 2 Hz bob: a transition, a drag or scroll settling, a hop, rain or snow.
bool animating(const App& a, const mock::Model& m, uint32_t ms);

// Touch, in screen coordinates, with each sample's own time (velocity comes from those times, not frame times).
void pointerDown(App& a, mock::Model& m, float x, float y, uint32_t ms);
void pointerMove(App& a, mock::Model& m, float x, float y, uint32_t ms);
void pointerUp(App& a, mock::Model& m, float x, float y, uint32_t ms);
void cycleScenario(App& a, mock::Model& m, uint32_t ms);   // the debug gesture
void cycleWeather(App& a, mock::Model& m, uint32_t ms);

}  // namespace ui
