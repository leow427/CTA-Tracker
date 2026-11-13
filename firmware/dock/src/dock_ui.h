#pragma once
// The dock's screens and gestures, ported from docs/ideas/2026-11-11-dock-ui-mockup.html (Leo, 2026-11-12): the
// stops screen (status strip and 3 stop cards), map mode, the 4-step stop picker, and the setup messages, in
// portrait 480 × 800. Plain C++ over gfx2d: the device feeds it touch and data and shows the frame; tools/preview.cpp
// renders it on a laptop.
#include <stddef.h>
#include <stdint.h>

#include "approach.h"
#include "bus_picker.h"
#include "cta_types.h"
#include "gfx2d.h"
#include "picks.h"

namespace dock {

constexpr int W = 480, H = 800;

// ---------- what the screens show (written by the network side, or the preview) ----------

struct Section {
    cta::PickedStop pick;          // id "" = an empty section
    cta::StopRecord rec;           // rec.ts 0 = no arrivals yet
    uint8_t mask = 0;              // lit LEDs (litMaskTrains / litMaskBuses), bit k = LED k + 1
    uint32_t posTs = 0;            // when the mask's positions were fetched, 0 = never
    cta::LatLon zone[7] = {};      // the row's stops on the map, farthest first; 0, 0 = unknown
};

constexpr int kMaxPath = 160;      // a bus route's points on the map

struct Data {
    Section sec[cta::kDockStops];
    cta::LatLon busPath[cta::kDockStops][kMaxPath];
    uint16_t busPathN[cta::kDockStops] = {};
    bool keys = true;              // CTA keys compiled in
    bool wifiSetUp = true;         // a network to join
    bool wifiUp = false;
    bool hasWeather = false;
    int wmo = 0;
    float tempC = 0;
    char status[40] = "";          // what the network side is doing when there's no data
    char name[16] = "CTA-Dock";    // the dock's name, for the setup message
};

// The picker's live bus lists (spec 7.2), fetched on request by the network side.
enum class Load : uint8_t { Idle, Loading, Ready, Failed };
struct PickerLists {
    static constexpr int kRoutes = 160, kDirs = 4, kStops = 220;
    Load routesState = Load::Idle, dirsState = Load::Idle, stopsState = Load::Idle;
    cta::BusRoute routes[kRoutes];
    int nRoutes = 0;
    cta::BusDirection dirs[kDirs];
    int nDirs = 0;
    cta::BusStop stops[kStops];
    int nStops = 0;
    // What each list answers (the request's route and direction): a reply to an earlier request stands for nothing.
    char dirsRoute[8] = "", stopsRoute[8] = "", stopsDir[40] = "";
};

// The time: Unix seconds (0 until the clock is set) and Chicago's local time (the TZ environment, spec 6.6).
struct Clock {
    uint32_t now = 0;
    bool night = false;
};

// ---------- the screen's own state ----------

enum class Screen : uint8_t { Home, Map, Picker };
enum class Act : uint8_t { None, Poke, MapToggle, OpenPicker, Back, Kind, Line, Route, Dir, Stop, Retry, Recenter, Home };
struct Hit { int16_t x, y, w, h; Act tap, hold; int16_t arg; };

// What the device must do after an input: fetch a picker list, or save a new pick.
enum class Request : uint8_t { None, Routes, Dirs, Stops };

struct App {
    Screen screen = Screen::Home;
    struct {
        int section = 0, step = 1;
        bool bus = false;
        char line[8] = "Red";          // the train line, or the bus route
        char lineName[26] = "";        // the bus route's name
        int dir = 0;
        char dirName[40] = "";         // the direction label (train) or rtdir (bus)
        float scroll = 0;
    } picker;
    int panX = 0, panY = 0;
    uint32_t pokeMs = 0;
    bool poked = false;
    // the press in progress
    struct {
        bool down = false, moved = false, held = false, edge = false;
        float x0 = 0, y0 = 0, x = 0, y = 0, s0 = 0;
        int panX0 = 0, panY0 = 0;
        uint32_t t0 = 0;
    } press;
    Hit hits[48];
    int nHits = 0;
    // out: for the device to act on, then clear
    Request request = Request::None;
    bool picked = false;
    int pickedSection = 0;
    cta::PickedStop pick;
    int debugMood = -1;                // -1 live; else a cta::Mood shown instead (the debug cycle)
};

// Picks and data can change between frames; draw() reads them as they are.
void draw(ui::Canvas& cv, App& a, const Data& d, const PickerLists& pl, const Clock& c, uint32_t ms);
// Long presses, from the press's own time. Call once per frame.
void tick(App& a, uint32_t ms);
void pointerDown(App& a, float x, float y, uint32_t ms);
void pointerMove(App& a, const PickerLists& pl, float x, float y, uint32_t ms);
void pointerUp(App& a, const Data& d, const PickerLists& pl, float x, float y, uint32_t ms);
// True while something on screen moves faster than the 2 Hz bob and blink (a drag).
bool animating(const App& a);

// cta::dueNow for a picked section (the LEDs' LED 7, and the map's vehicle at the stop).
bool dueNow(const Section& s, uint32_t now);
// cta::dockMood and cta::moodReason over the picked sections.
cta::Mood moodOf(const Data& d, const Clock& c);
// Trackie's reason for his mood ("Red in 3 min"), for a tap on him.
void reasonText(const Data& d, const Clock& c, char* out, size_t cap);

}  // namespace dock
