// Renders the dock's screens to PNGs on a laptop, from the same dock_ui code the device runs, with the mockup's
// sample data (docs/ideas/2026-11-11-dock-ui-mockup.html): the stops screen in each mood, Wi-Fi lost, night, empty
// sections, the map, the picker's steps, and the setup messages. Each is also drawn the device's way (bands of rows
// straight in the landscape panel's order, both mountings, bands.cpp) and checked against the plain picture.
// Build and run (from firmware/dock):
//   c++ -std=c++17 -O2 -Isrc -Iinclude -I../lib/gfx/src -I../lib/cta/src -I../lib/pet/src -I<ArduinoJson>/src \
//       tools/preview.cpp src/dock_ui.cpp src/bands.cpp ../lib/gfx/src/*.cpp ../lib/cta/src/*.cpp \
//       ../lib/pet/src/*.cpp -lz -o /tmp/dock_preview
//   /tmp/dock_preview <out dir>
// (<ArduinoJson> is any ArduinoJson 7 checkout, e.g. .pio/libdeps/dock/ArduinoJson after a pio run.)
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
};

}  // namespace

static bool bandsMatch(const uint16_t* fb, dock::App& a, const dock::Data& d, const dock::PickerLists& pl,
                       const dock::Clock& c) {
    static uint16_t panel[dock::W * dock::H], band[dock::W * 32];
    for (int cw = 0; cw < 2; ++cw) {
        for (int y0 = 0; y0 < dock::H; y0 += 32) {
            ui::Canvas cv = dock::bandCanvas(band, y0, 32, cw != 0);
            dock::draw(cv, a, d, pl, c, 600);
            dock::copyBand(band, panel, y0, 32, cw != 0);
        }
        for (int y = 0; y < dock::H; ++y)
            for (int x = 0; x < dock::W; ++x) {
                const int X = cw ? y : dock::H - 1 - y, Y = cw ? dock::W - 1 - x : x;   // bands.h's mapping
                if (panel[Y * dock::H + X] != fb[y * dock::W + x]) return false;
            }
    }
    return true;
}

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
        const bool same = bandsMatch(fb, a, d, pl, c);
        printf("%s: mood %d, reason \"%s\", bands %s\n", st.name, static_cast<int>(dock::moodOf(d, c)), reason,
               same ? "match" : "DIFFER");
        if (!same) bad = true;
    }
    return bad ? 1 : 0;
}
