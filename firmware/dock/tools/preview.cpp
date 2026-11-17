// Renders the dock's screens to PNGs on a laptop, from the same dock_ui code the device runs, with the mockup's
// sample data (docs/ideas/2026-11-11-dock-ui-mockup.html): the stops screen in each mood, Wi-Fi lost, night, empty
// sections, the map, the picker's steps, and the setup messages. Each is also drawn the device's way (bands.cpp:
// recorded once and played band by band, and every band drawn directly, straight in the landscape panel's order, both
// mountings, at both halves of Trackie's bob and the blinks) and checked against the plain picture. Then a run of
// frames goes through the device's two frame buffers, each drawing only the bands it doesn't hold, and every frame is
// checked against the plain picture; it prints how many bands each kind of frame drew. Both checks also run with the
// bands copied late, as the dock's DMA copies them (dma_copy.h), through two band buffers.
// Build and run (from firmware/dock):
//   cc -O2 -c ../native/lib/cJSON/cJSON.c -o /tmp/cJSON.o
//   c++ -std=c++17 -O2 -Isrc -Iinclude -I../lib/gfx/src -I../lib/cta/src -I../lib/pet/src -I../native/lib/cJSON \
//       tools/preview.cpp src/dock_ui.cpp src/bands.cpp ../lib/gfx/src/*.cpp ../lib/cta/src/*.cpp \
//       ../lib/pet/src/*.cpp /tmp/cJSON.o -lz -o /tmp/dock_preview
//   /tmp/dock_preview <out dir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#include <string>
#include <vector>

#include "approach.h"
#include "cta_config.h"
#include "dock_ui.h"
#include "bands.h"

namespace {

void put32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((x >> s) & 255); }
void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> v;
    put32(v, data.size());
    v.insert(v.end(), type, type + 4);
    v.insert(v.end(), data.begin(), data.end());
    put32(v, crc32(0, v.data() + 4, v.size() - 4));
    fwrite(v.data(), 1, v.size(), f);
}

void writePng(const std::string& path, const uint16_t* px) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < dock::H; ++y) {
        raw.push_back(0);
        for (int x = 0; x < dock::W; ++x) {
            const uint16_t c = px[y * dock::W + x];
            raw.push_back(((c >> 11) & 31) << 3);
            raw.push_back(((c >> 5) & 63) << 2);
            raw.push_back((c & 31) << 3);
        }
    }
    uLongf n = compressBound(raw.size());
    std::vector<uint8_t> z(n);
    compress2(z.data(), &n, raw.data(), raw.size(), 6);
    z.resize(n);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); exit(1); }
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> hdr;
    put32(hdr, dock::W); put32(hdr, dock::H);
    hdr.insert(hdr.end(), {8, 2, 0, 0, 0});
    chunk(f, "IHDR", hdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    fclose(f);
}

// 2026-11-11 19:42 in Chicago (CST, UTC-6): the mockup's fixed evening.
constexpr uint32_t kNow = 1794447720;

void arrival(cta::StopRecord& r, int min, const char* dest) {
    cta::Arrival& a = r.arrivals[r.count++];
    a.min = static_cast<int16_t>(min);
    snprintf(a.dest, sizeof(a.dest), "%s", dest);
    a.live = true;
    a.delayed = false;
}

void sampleData(dock::Data& d) {
    d = dock::Data();
    cta::PickedStop picks[cta::kDockStops];
    cta::defaultPicks(picks);
    for (int i = 0; i < cta::kDockStops; ++i) {
        dock::Section& s = d.sec[i];
        s.pick = picks[i];
        memset(&s.rec, 0, sizeof(s.rec));
        s.rec.ts = kNow;
        s.posTs = kNow;
        cta::ApproachRow row;
        if (picks[i].type == cta::Kind::Train && cta::trainRowFor(picks[i].route, picks[i].id, row))
            for (int k = 0; k < 7; ++k) s.zone[k] = {row.lat[k], row.lon[k]};
    }
    arrival(d.sec[0].rec, 3, "95th/Dan Ryan"); arrival(d.sec[0].rec, 9, "95th/Dan Ryan"); arrival(d.sec[0].rec, 15, "Roosevelt");
    arrival(d.sec[1].rec, 6, "Loop"); arrival(d.sec[1].rec, 13, "Loop"); arrival(d.sec[1].rec, 21, "Loop");
    arrival(d.sec[2].rec, 4, "Harrison"); arrival(d.sec[2].rec, 12, "Harrison"); arrival(d.sec[2].rec, 23, "Polk");
    d.sec[0].mask = (1 << 1) | (1 << 4);   // the mockup's LEDs 2 and 5
    d.sec[1].mask = (1 << 0) | (1 << 3);
    d.sec[2].mask = (1 << 2) | (1 << 5);
    // The 22's Southbound stops before Clark & Belmont, and its line along Clark, as getpatterns would give them.
    const cta::LatLon zone[7] = {{41.950917f, -87.659607f}, {41.949106f, -87.658133f}, {41.947362f, -87.656756f},
                                 {41.945662f, -87.655361f}, {41.943617f, -87.653725f}, {41.941897f, -87.652317f},
                                 {41.940088f, -87.650909f}};
    memcpy(d.sec[2].zone, zone, sizeof(zone));
    const cta::LatLon path[] = {{41.9614f, -87.6681f}, {41.954247f, -87.662197f}, {41.95267f, -87.661014f},
                                {41.950917f, -87.659607f}, {41.947362f, -87.656756f}, {41.943617f, -87.653725f},
                                {41.940088f, -87.650909f}, {41.9363f, -87.6479f}, {41.9325f, -87.6455f}, {41.9259f, -87.6405f}};
    d.busPathN[2] = sizeof(path) / sizeof(path[0]);
    memcpy(d.busPath[2], path, sizeof(path));
    d.wifiUp = true;
    d.hasWeather = true;
    d.wmo = 0;
    d.tempC = 5;   // 41 °F
    snprintf(d.name, sizeof(d.name), "CTA-Dock-3F2A");
    snprintf(d.ssid, sizeof(d.ssid), "Home Wi-Fi");   // a placeholder: never a real network's name
    d.rssi = -61;
}

struct State {
    const char* name;
    void (*setup)(dock::Data&, dock::App&, dock::PickerLists&, dock::Clock&);
};

void sampleLists(dock::PickerLists& pl) {
    const char* routes[][2] = {{"22", "Clark"}, {"36", "Broadway"}, {"8", "Halsted"}, {"77", "Belmont"}, {"151", "Sheridan"},
                               {"9", "Ashland"}, {"152", "Addison"}, {"146", "Inner Lake Shore/Michigan Express"}};
    pl.nRoutes = 8;
    for (int k = 0; k < 8; ++k) {
        snprintf(pl.routes[k].rt, sizeof(pl.routes[k].rt), "%s", routes[k][0]);
        snprintf(pl.routes[k].name, sizeof(pl.routes[k].name), "%.24s", routes[k][1]);
    }
    pl.routesState = dock::Load::Ready;
    pl.nDirs = 2;
    snprintf(pl.dirs[0].name, sizeof(pl.dirs[0].name), "Northbound");
    snprintf(pl.dirs[1].name, sizeof(pl.dirs[1].name), "Southbound");
    pl.dirsState = dock::Load::Ready;
    snprintf(pl.dirsRoute, sizeof(pl.dirsRoute), "22");
    const char* stops[] = {"Clark & Devon", "Clark & Granville", "Clark & Foster", "Clark & Lawrence", "Clark & Montrose",
                           "Clark & Irving Park", "Clark & Addison", "Clark & Belmont", "Clark & Diversey", "Clark & Fullerton",
                           "Clark & Armitage", "Clark & North"};
    pl.nStops = 12;
    for (int k = 0; k < 12; ++k) {
        snprintf(pl.stops[k].id, sizeof(pl.stops[k].id), "%d", 1800 + k);
        snprintf(pl.stops[k].name, sizeof(pl.stops[k].name), "%s", stops[k]);
    }
    pl.stopsState = dock::Load::Ready;
    snprintf(pl.stopsRoute, sizeof(pl.stopsRoute), "22");
    snprintf(pl.stopsDir, sizeof(pl.stopsDir), "Southbound");
}

void picker(dock::App& a, int step, bool bus, float scroll = 0) {
    a.screen = dock::Screen::Picker;
    a.picker.step = step;
    a.picker.bus = bus;
    snprintf(a.picker.line, sizeof(a.picker.line), bus ? "22" : "Red");
    snprintf(a.picker.lineName, sizeof(a.picker.lineName), bus ? "Clark" : "");
    snprintf(a.picker.dirName, sizeof(a.picker.dirName), bus ? "Southbound" : "95th/Dan Ryan");
    a.picker.scroll = scroll;
}

// The touch extras (ecosystem spec 5.10): section i's first arrival's stops ahead, open and answered.
void sampleAhead(dock::Data& d, dock::App& a, int i) {
    for (int k = 0; k < 3; ++k) snprintf(d.sec[i].rec.arrivals[k].vehicle, sizeof(d.sec[i].rec.arrivals[k].vehicle), "%d", 811 + k);
    a.ahead.section = i;
    snprintf(a.ahead.vehicle, sizeof(a.ahead.vehicle), "811");
    a.ahead.id = d.ahead.id = 7;
    a.ahead.ms = 0;
    d.ahead.state = dock::Load::Ready;
    d.ahead.ts = kNow;
    const bool bus = i == 2;
    const char* trains[] = {"Addison", "Sheridan", "Wilson", "Lawrence", "Argyle"};
    const char* buses[] = {"Clark & Addison", "Clark & Roscoe", "Clark & Belmont", "Clark & Wellington", "Clark & Diversey"};
    const int mins[] = {0, 1, 3, 4, 6};
    d.ahead.n = 5;
    for (int k = 0; k < 5; ++k) {
        snprintf(d.ahead.stops[k].name, sizeof(d.ahead.stops[k].name), "%s", bus ? buses[k] : trains[k]);
        d.ahead.stops[k].min = static_cast<int16_t>(mins[k]);
    }
}

// Section i peeking at the other way, answered.
void samplePeek(dock::Data& d, dock::App& a, int i) {
    a.peek.section = i;
    a.peek.id = d.peek.id = 9;
    a.peek.ms = 0;
    d.peek.state = dock::Load::Ready;
    d.peek.pick = d.sec[i].pick;
    d.peek.rec = d.sec[i].rec;
    const bool bus = i == 2;
    snprintf(d.peek.pick.dir, sizeof(d.peek.pick.dir), "%s", bus ? "Northbound" : i == 0 ? "Howard" : "Kimball");
    const char* dest = bus ? "Arthur" : i == 0 ? "Howard" : "Kimball";
    for (int k = 0; k < d.peek.rec.count; ++k) {
        snprintf(d.peek.rec.arrivals[k].dest, sizeof(d.peek.rec.arrivals[k].dest), "%s", dest);
        d.peek.rec.arrivals[k].min = static_cast<int16_t>(2 + 7 * k);
    }
}

const State kStates[] = {
    {"home_happy", [](dock::Data&, dock::App&, dock::PickerLists&, dock::Clock&) {}},
    {"home_hyped_rain", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) { d.sec[0].rec.arrivals[0].min = 2; d.wmo = 61; d.tempC = 9; }},
    {"home_worried_snow", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) {
         d.sec[2].rec.arrivals[0].delayed = true; d.sec[2].rec.arrivals[0].min += 4; d.sec[2].rec.arrivals[1].min += 4;
         d.wmo = 73; d.tempC = -3; }},
    {"home_sad_alert", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { d.sec[0].rec.alert = true; a.poked = true; }},
    {"home_sched", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) {
         for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k) { d.sec[i].rec.arrivals[k].live = false; d.sec[i].rec.arrivals[k].min = 18 + i * 7 + k * 15; } }},
    {"home_night", [](dock::Data&, dock::App&, dock::PickerLists&, dock::Clock& c) { c.night = true; }},
    {"home_dim", [](dock::Data&, dock::App&, dock::PickerLists&, dock::Clock& c) { c.screen = 40; }},
    {"home_wifi_lost", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock& c) { d.wifiUp = false; c.now += 7 * 60; }},
    {"home_section3_empty", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) { memset(&d.sec[2].pick, 0, sizeof(d.sec[2].pick)); }},
    {"home_none", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) { for (auto& s : d.sec) memset(&s.pick, 0, sizeof(s.pick)); }},
    {"home_waiting", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) {
         for (auto& s : d.sec) s.rec.ts = 0; snprintf(d.status, sizeof(d.status), "Connecting to Wi-Fi"); d.hasWeather = false; }},
    {"map", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { a.screen = dock::Screen::Map; }},
    {"map_hyped_rain", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { a.screen = dock::Screen::Map; d.sec[0].rec.arrivals[0].min = 2; d.wmo = 61; }},
    {"map_wifi_lost", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock& c) { a.screen = dock::Screen::Map; d.wifiUp = false; c.now += 7 * 60; }},
    {"map_section3_empty", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { a.screen = dock::Screen::Map; memset(&d.sec[2].pick, 0, sizeof(d.sec[2].pick)); }},
    {"map_panned", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { a.screen = dock::Screen::Map; a.panX = -60; a.panY = 120; }},
    {"picker1_kind", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { picker(a, 1, false); }},
    {"picker2_line", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { picker(a, 2, false); }},
    {"picker2_route", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { picker(a, 2, true); }},
    {"picker2_route_loading", [](dock::Data&, dock::App& a, dock::PickerLists& pl, dock::Clock&) { picker(a, 2, true); pl.routesState = dock::Load::Loading; }},
    {"picker3_dir", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { picker(a, 3, false); }},
    {"picker3_dir_green", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { picker(a, 3, false); snprintf(a.picker.line, sizeof(a.picker.line), "G"); }},
    {"picker4_stop", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { picker(a, 4, false, 532); }},
    {"picker4_bus_stop", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { picker(a, 4, true, 100); }},
    {"picker4_failed", [](dock::Data&, dock::App& a, dock::PickerLists& pl, dock::Clock&) { picker(a, 4, true); pl.stopsState = dock::Load::Failed; }},
    {"no_wifi", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) { d.wifiSetUp = false; }},
    {"no_key", [](dock::Data& d, dock::App&, dock::PickerLists&, dock::Clock&) { d.keys = false; }},
    {"pairing", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { snprintf(a.pairCode, sizeof(a.pairCode), "042917"); }},
    {"pair_cooldown", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { a.pairWait = 23; }},
    {"settings", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) { a.screen = dock::Screen::Settings; a.paired = true; }},
    {"settings_confirm", [](dock::Data&, dock::App& a, dock::PickerLists&, dock::Clock&) {
         a.screen = dock::Screen::Settings; a.paired = true; a.pairAsk = true; }},
    {"settings_offline", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { a.screen = dock::Screen::Settings; d.wifiUp = false; }},
    {"home_ahead", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { sampleAhead(d, a, 0); }},
    {"home_ahead_bus", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { sampleAhead(d, a, 2); }},
    {"home_ahead_loading", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) {
         sampleAhead(d, a, 0); d.ahead.state = dock::Load::Loading; }},
    {"home_ahead_failed", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) {
         sampleAhead(d, a, 0); d.ahead.state = dock::Load::Failed; d.ahead.n = 0; }},
    {"home_peek", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { samplePeek(d, a, 0); }},
    {"home_peek_bus", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) { samplePeek(d, a, 2); }},
    {"home_peek_loading", [](dock::Data& d, dock::App& a, dock::PickerLists&, dock::Clock&) {
         samplePeek(d, a, 1); d.peek.state = dock::Load::Loading; }},
};

// ---------- the device's path (bands.cpp, display.cpp) ----------

ui::Recording rec;   // big, like the device's
uint16_t band[dock::W * dock::kBandRows], band2[dock::W * dock::kBandRows];
uint32_t noClock() { return 0; }

// A stand-in for the dock's DMA copier (dma_copy.h): each band is copied only when the next copy starts, or at finish,
// as the DMA's copy lands while the next band is drawn. A band drawn into a buffer whose copy hasn't landed yet would
// copy the wrong pixels and show as a difference.
struct Pending { const uint16_t* band; uint16_t* fb; int y0, rows; bool cw; } pending = {};
void land() {
    if (pending.band) dock::copyBand(pending.band, pending.fb, pending.y0, pending.rows, pending.cw);
    pending.band = nullptr;
}
const dock::BandCopier kLateCopier = {
    [](const uint16_t* b, uint16_t* fb, int y0, int rows, bool cw) { land(); pending = {b, fb, y0, rows, cw}; },
    [](uint16_t*) { land(); }};

struct Frame {
    dock::App* a;
    const dock::Data* d;
    const dock::PickerLists* pl;
    const dock::Clock* c;
    uint32_t ms;
};
void drawFrameCb(ui::Canvas& cv, void* p) {
    const Frame& f = *static_cast<Frame*>(p);
    dock::draw(cv, *f.a, *f.d, *f.pl, *f.c, f.ms);
}

// A panel frame buffer against the plain picture, through bands.h's mapping.
bool samePicture(const uint16_t* panel, const uint16_t* fb, bool cw) {
    for (int y = 0; y < dock::H; ++y)
        for (int x = 0; x < dock::W; ++x) {
            const int X = cw ? y : dock::H - 1 - y, Y = cw ? dock::W - 1 - x : x;
            if (panel[Y * dock::H + X] != fb[y * dock::W + x]) return false;
        }
    return true;
}

// The frame drawn the device's way into a blank frame buffer, both mountings: recorded and played band by band, and
// (for a frame that can't be recorded) every band drawn directly.
bool bandsMatch(const uint16_t* fb, dock::App& a, const dock::Data& d, const dock::PickerLists& pl,
                const dock::Clock& c, uint32_t ms) {
    static uint16_t panel[dock::W * dock::H];
    static dock::BandMemo memo;
    Frame f = {&a, &d, &pl, &c, ms};
    for (int cw = 0; cw < 2; ++cw)
        for (int recorded = 0; recorded < 2; ++recorded)
            for (int late = 0; late < 2; ++late) {   // copied at once, or by the stand-in for the DMA
                memset(panel, 0, sizeof(panel));
                memo.forget();
                dock::FrameStats st;
                dock::drawFrame(drawFrameCb, &f, recorded ? &rec : nullptr, memo, 0, band, panel, cw != 0, noClock, st,
                                late ? band2 : nullptr, late ? &kLateCopier : nullptr);
                if (st.drawn != dock::BandMemo::kBands || pending.band || !samePicture(panel, fb, cw != 0)) return false;
            }
    return true;
}

// A run of frames as the device shows them, each drawn into the buffer the panel isn't showing (the two take turns)
// and drawing only the bands that buffer doesn't hold: at rest (Trackie's bob, the blinks), a poke and its end, the
// clock's minute, new arrivals, the map and a pan, the picker and a scroll, Wi-Fi lost, night. After each frame the
// buffer must be exactly the frame drawn whole. Prints how many bands each kind of frame drew.
bool sequenceMatches(bool late) {
    static uint16_t panels[2][dock::W * dock::H], fb[dock::W * dock::H];
    static dock::Data d;
    static dock::PickerLists pl;
    static dock::BandMemo memo;
    sampleData(d);
    sampleLists(pl);
    dock::App a;
    dock::Clock c;
    c.now = kNow;
    uint32_t ms = 0;
    struct Step { const char* name; int frames; void (*apply)(dock::Data&, dock::App&, dock::Clock&, uint32_t&); };
    const Step steps[] = {
        {"first frames", 2, [](dock::Data&, dock::App&, dock::Clock&, uint32_t& ms) { ms += 500; }},
        {"at rest", 8, [](dock::Data&, dock::App&, dock::Clock&, uint32_t& ms) { ms += 500; }},
        {"poke", 1, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) { ms += 120; a.poked = true; a.pokeMs = ms; }},
        {"poked, at rest", 6, [](dock::Data&, dock::App&, dock::Clock&, uint32_t& ms) { ms += 500; }},
        {"minute", 1, [](dock::Data&, dock::App&, dock::Clock& c, uint32_t& ms) { ms += 500; c.now += 60; }},
        {"arrivals", 1, [](dock::Data& d, dock::App&, dock::Clock&, uint32_t& ms) {
             ms += 37; for (auto& s : d.sec) for (int k = 0; k < s.rec.count; ++k) s.rec.arrivals[k].min -= 1; }},
        {"at rest", 4, [](dock::Data&, dock::App&, dock::Clock&, uint32_t& ms) { ms += 500; }},
        {"map", 1, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) { ms += 90; a.screen = dock::Screen::Map; }},
        {"map pan", 12, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) { ms += 45; a.panX += 9; a.panY -= 5; }},
        {"map at rest", 4, [](dock::Data&, dock::App&, dock::Clock&, uint32_t& ms) { ms += 500; }},
        {"stops", 1, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) { ms += 70; a.screen = dock::Screen::Home; }},
        {"picker", 1, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) {
             ms += 800; a.screen = dock::Screen::Picker; a.picker.step = 1; a.picker.bus = false; }},
        {"picker line", 1, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) { ms += 300; a.picker.step = 2; }},
        {"picker stops", 1, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) {
             ms += 300; a.picker.step = 4; snprintf(a.picker.line, sizeof(a.picker.line), "Red");
             snprintf(a.picker.dirName, sizeof(a.picker.dirName), "95th/Dan Ryan"); a.picker.scroll = 0; }},
        {"picker scroll", 12, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) { ms += 40; a.picker.scroll += 33.3f; }},
        {"picker at rest", 4, [](dock::Data&, dock::App&, dock::Clock&, uint32_t& ms) { ms += 500; }},
        {"stops", 1, [](dock::Data&, dock::App& a, dock::Clock&, uint32_t& ms) { ms += 60; a.screen = dock::Screen::Home; }},
        {"Wi-Fi lost", 3, [](dock::Data& d, dock::App&, dock::Clock& c, uint32_t& ms) { ms += 500; d.wifiUp = false; c.now += 200; }},
        {"night", 3, [](dock::Data& d, dock::App&, dock::Clock& c, uint32_t& ms) { ms += 500; d.wifiUp = true; c.night = true; }},
        {"bus picker list", 3, [](dock::Data&, dock::App& a, dock::Clock& c, uint32_t& ms) {
             ms += 500; c.night = false; a.screen = dock::Screen::Picker; a.picker.step = 2; a.picker.bus = true; a.picker.scroll = 0; }},
    };
    memset(panels, 0, sizeof(panels));
    memo.forget();
    int back = 0;
    bool ok = true;
    for (const Step& s : steps) {
        int drawn = 0, copied = 0;
        for (int k = 0; k < s.frames; ++k) {
            s.apply(d, a, c, ms);
            Frame f = {&a, &d, &pl, &c, ms};
            dock::FrameStats st;
            dock::drawFrame(drawFrameCb, &f, &rec, memo, back, band, panels[back], true, noClock, st, late ? band2 : nullptr,
                            late ? &kLateCopier : nullptr);
            drawn += st.drawn;
            copied += st.copied;
            dock::App plain = a;
            ui::Canvas cv(fb, dock::W, dock::H);
            dock::draw(cv, plain, d, pl, c, ms);
            if (pending.band || !samePicture(panels[back], fb, true)) {
                printf("sequence%s: %s, frame %d: DIFFERS\n", late ? " (late copies)" : "", s.name, k + 1);
                ok = false;
            }
            back ^= 1;
        }
        if (!late)
            printf("sequence: %-16s %2d frame%-2s %4.1f bands drawn and %4.1f copied a frame\n", s.name, s.frames,
                   s.frames == 1 ? "," : "s,", static_cast<float>(drawn) / s.frames, static_cast<float>(copied) / s.frames);
    }
    return ok;
}

// Settings by touch: the gear opens it, back returns Home, "Pair again" asks first, Cancel keeps the token, and the
// confirm asks the device to forget it. Also the Wi-Fi bars' thresholds.
bool settingsFlow() {
    static uint16_t fb[dock::W * dock::H];
    static dock::Data d;
    static dock::PickerLists pl;
    sampleData(d);
    dock::App a;
    dock::Clock c;
    c.now = kNow;
    uint32_t ms = 1000;
    auto tap = [&](float x, float y) {
        ui::Canvas cv(fb, dock::W, dock::H);
        dock::draw(cv, a, d, pl, c, ms);   // the hits are the last frame's
        dock::pointerDown(a, x, y, ms);
        dock::pointerUp(a, d, pl, x, y, ms += 80);
        ms += 500;
    };
    bool ok = true;
    auto expect = [&](bool v, const char* what) { if (!v) { printf("settings flow: %s FAILED\n", what); ok = false; } };
    tap(280, 40);
    expect(a.screen == dock::Screen::Settings, "the gear opens Settings");
    tap(30, 40);
    expect(a.screen == dock::Screen::Home, "back returns Home");
    a.poked = true; a.pokeMs = ms;
    tap(280, 40);
    expect(a.screen == dock::Screen::Home, "no gear while poked");
    a.poked = false;
    tap(280, 40);
    tap(240, 488);
    expect(a.pairAsk && !a.pairAgain, "Pair again asks first");
    tap(30, 40);
    expect(a.screen == dock::Screen::Settings, "back is covered by the confirm");
    tap(144, 436);
    expect(!a.pairAsk && !a.pairAgain && a.screen == dock::Screen::Settings, "Cancel keeps the token");
    tap(240, 488);
    tap(336, 436);
    expect(a.pairAgain && !a.pairAsk && a.screen == dock::Screen::Home, "the confirm pairs again");
    const int rssi[] = {0, -40, -55, -56, -66, -67, -77, -78, -95}, bars[] = {0, 4, 4, 3, 3, 2, 2, 1, 1};
    for (int k = 0; k < 9; ++k) expect(dock::wifiBars(rssi[k]) == bars[k], "Wi-Fi bars");
    printf("settings flow: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

// The touch extras by touch (ecosystem spec 5.10): a time opens its stops ahead and asks once; a tap closes it, as do
// 30 s and the vehicle leaving the card; a card's tap peeks the other way, a second tap or 30 s ends it; a hold still
// opens the picker; nothing touches the picks.
bool extrasFlow() {
    static uint16_t fb[dock::W * dock::H];
    static dock::Data d;
    static dock::PickerLists pl;
    sampleData(d);
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) snprintf(d.sec[i].rec.arrivals[k].vehicle, sizeof(d.sec[i].rec.arrivals[k].vehicle), "%d", 100 * i + k);
    dock::App a;
    dock::Clock c;
    c.now = kNow;
    uint32_t ms = 1000;
    auto frame = [&]() { ui::Canvas cv(fb, dock::W, dock::H); dock::draw(cv, a, d, pl, c, ms); };
    auto press = [&](float x, float y, uint32_t held) {
        frame();   // the hits are the last frame's
        dock::pointerDown(a, x, y, ms);
        ms += held;
        dock::tick(a, ms);
        dock::pointerUp(a, d, pl, x, y, ms);
        ms += 500;
    };
    auto tap = [&](float x, float y) { press(x, y, 80); };
    bool ok = true;
    auto expect = [&](bool v, const char* what) { if (!v) { printf("extras flow: %s FAILED\n", what); ok = false; } };
    // section 1's middle tile: card top 88, tiles at +108, the second at x 30 + 152
    tap(240, 88 + 108 + 50);
    expect(a.ahead.id && a.ahead.ask && a.ahead.section == 0 && strcmp(a.ahead.vehicle, "1") == 0, "a time asks for its stops ahead");
    a.ahead.ask = false;
    tap(240, 300);
    expect(!a.ahead.id, "a tap closes the stops ahead");
    tap(240, 88 + 108 + 50);
    ms += 31000;
    frame();
    expect(!a.ahead.id, "the stops ahead close after 30 s");
    tap(240, 88 + 108 + 50);
    d.sec[0].rec.arrivals[1].vehicle[0] = 'x';
    frame();
    expect(!a.ahead.id, "the stops ahead close when the vehicle leaves the card");
    d.sec[0].rec.arrivals[1].vehicle[0] = '1';
    tap(240, 88 + 108 + 50);
    d.ahead.id = a.ahead.id; d.ahead.state = dock::Load::Ready; d.ahead.gone = true;
    frame();
    expect(!a.ahead.id, "the stops ahead close when the vehicle leaves service");
    // section 2's card outside its times
    tap(240, 328 + 40);
    expect(a.peek.id && a.peek.ask && a.peek.section == 1 && !a.ahead.id, "a card's tap peeks the other way");
    a.peek.ask = false;
    d.peek.id = a.peek.id; d.peek.state = dock::Load::Ready; d.peek.pick = d.sec[1].pick; d.peek.rec = d.sec[1].rec;
    tap(240, 328 + 108 + 50);
    expect(!a.ahead.id && !a.peek.id, "a second tap ends the peek, on its times too");
    expect(!a.picked, "a peek saves no pick");
    tap(240, 328 + 40);
    ms += 31000;
    frame();
    expect(!a.peek.id, "the peek ends after 30 s");
    press(240, 328 + 40, 900);
    expect(a.screen == dock::Screen::Picker && a.picker.section == 1 && !a.picked, "a hold still opens the picker");
    printf("extras flow: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <out dir>\n", argv[0]); return 2; }
    setenv("TZ", cta::kTz, 1);
    tzset();
    static uint16_t fb[dock::W * dock::H];
    static dock::Data d;
    static dock::PickerLists pl;
    bool bad = false;
    for (const State& st : kStates) {
        sampleData(d);
        pl = dock::PickerLists();
        sampleLists(pl);
        dock::App a;
        dock::Clock c;
        c.now = kNow;
        st.setup(d, a, pl, c);
        ui::Canvas cv(fb, dock::W, dock::H);
        dock::draw(cv, a, d, pl, c, 600);
        writePng(std::string(argv[1]) + "/" + st.name + ".png", fb);
        char reason[48];
        dock::reasonText(d, c, reason, sizeof(reason));
        bool same = bandsMatch(fb, a, d, pl, c, 600);
        dock::draw(cv, a, d, pl, c, 0);   // Trackie's other bob and the blinks' other half
        same = same && bandsMatch(fb, a, d, pl, c, 0);
        printf("%s: mood %d, reason \"%s\", bands %s\n", st.name, static_cast<int>(dock::moodOf(d, c)), reason,
               same ? "match" : "DIFFER");
        if (!same) bad = true;
    }
    if (!sequenceMatches(false) || !sequenceMatches(true)) bad = true;
    if (!settingsFlow()) bad = true;
    if (!extrasFlow()) bad = true;
    return bad ? 1 : 0;
}
