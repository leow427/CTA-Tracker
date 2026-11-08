// Renders the mock's screens to PNGs on a laptop, from the same ui/mock code the device runs.
// Build and run (from firmware/handheld/mock):
//   c++ -std=c++17 -O2 -Isrc -I../../lib/cta/src -I../../lib/pet/src -I<ArduinoJson>/src tools/preview.cpp \
//       src/gfx2d.cpp src/fonts.cpp src/ui.cpp src/mock_data.cpp ../../lib/cta/src/*.cpp ../../lib/pet/src/*.cpp \
//       -lz -o /tmp/mock_preview && /tmp/mock_preview <out dir> [--strips]
// --strips draws each frame the way the device does (48-row strips in the panel's rotated, byte-swapped layout)
// and turns it back, so the PNGs should match the plain ones exactly.
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <string>
#include <vector>

#include "ui.h"

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

// brightness mimics the panel's: the mockup darkens the frame the same way.
void writePng(const std::string& path, const uint16_t* px, float brightness) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < ui::W; ++y) {
        raw.push_back(0);
        for (int x = 0; x < ui::W; ++x) {
            const uint16_t c = px[y * ui::W + x];
            const int dx = x - ui::C, dy = y - ui::C;
            const bool in = dx * dx + dy * dy <= ui::C * ui::C;
            const uint8_t r = ((c >> 11) & 31) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
            raw.push_back(in ? r * brightness : 24);
            raw.push_back(in ? g * brightness : 24);
            raw.push_back(in ? b * brightness : 24);
        }
    }
    uLongf n = compressBound(raw.size());
    std::vector<uint8_t> z(n);
    compress2(z.data(), &n, raw.data(), raw.size(), 6);
    z.resize(n);
    FILE* f = fopen(path.c_str(), "wb");
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> hdr;
    put32(hdr, ui::W); put32(hdr, ui::W);
    hdr.insert(hdr.end(), {8, 2, 0, 0, 0});
    chunk(f, "IHDR", hdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    fclose(f);
}

// Draws the frame in panel strips, as main.cpp does, and un-rotates them into fb.
void drawStrips(uint16_t* fb, ui::App& a, const mock::Model& m, uint32_t ms, uint16_t* scratch) {
    constexpr int kRows = 48;
    static uint16_t strip[ui::W * kRows];
    for (int y0 = 0; y0 < ui::W; y0 += kRows) {
        const int rows = y0 + kRows <= ui::W ? kRows : ui::W - y0;
        ui::Canvas cv = ui::Canvas::panelBand(strip, ui::W, y0, rows);
        ui::draw(cv, a, m, ms, scratch);
        for (int x = 0; x < ui::W; ++x)
            for (int y = y0; y < y0 + rows; ++y) {
                const uint16_t c = strip[x * rows + (y0 + rows - 1 - y)];
                fb[y * ui::W + x] = static_cast<uint16_t>(c << 8 | c >> 8);
            }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    const bool strips = argc > 2 && strcmp(argv[2], "--strips") == 0;
    static uint16_t fb[ui::W * ui::W];
    static uint16_t scratch[ui::kScratchPixels];
    ui::Canvas cv(fb, ui::W, ui::W);
    const uint32_t start = 1794019260;   // 2026-11-06 20:41 Chicago
    struct Shot { const char* name; mock::Scenario sc; ui::Screen screen; ui::Tab tab; float scroll; bool dim; mock::Weather w; int detailIdx; uint32_t after; };
    const Shot shots[] = {
        {"home_live", mock::Scenario::Live, ui::Screen::Home, ui::Tab::Trains, 0, false, mock::Weather::Clear, -1, 40},
        {"home_happy", mock::Scenario::Happy, ui::Screen::Home, ui::Tab::Trains, 0, false, mock::Weather::Clear, -1, 0},
        {"home_worried", mock::Scenario::Worried, ui::Screen::Home, ui::Tab::Trains, 0, false, mock::Weather::Rain, -1, 0},
        {"home_sad", mock::Scenario::Sad, ui::Screen::Home, ui::Tab::Trains, 0, false, mock::Weather::Snow, -1, 0},
        {"home_offline", mock::Scenario::Offline, ui::Screen::Home, ui::Tab::Trains, 0, false, mock::Weather::Clear, -1, 0},
        {"home_dim", mock::Scenario::Live, ui::Screen::Home, ui::Tab::Trains, 0, true, mock::Weather::Clear, -1, 0},
        {"docked", mock::Scenario::Docked, ui::Screen::Home, ui::Tab::Trains, 0, false, mock::Weather::Clear, -1, 0},
        {"trains", mock::Scenario::Live, ui::Screen::Stops, ui::Tab::Trains, 0, false, mock::Weather::Clear, -1, 0},
        {"trains_scrolled", mock::Scenario::Sad, ui::Screen::Stops, ui::Tab::Trains, 112 * 3, false, mock::Weather::Clear, -1, 0},
        {"buses", mock::Scenario::Worried, ui::Screen::Stops, ui::Tab::Buses, 0, false, mock::Weather::Clear, -1, 0},
        {"detail_train", mock::Scenario::Sad, ui::Screen::Detail, ui::Tab::Trains, 0, false, mock::Weather::Clear, 0, 0},
        {"detail_bus", mock::Scenario::Worried, ui::Screen::Detail, ui::Tab::Buses, 0, false, mock::Weather::Clear, 1, 0},
        {"pairing", mock::Scenario::Pairing, ui::Screen::Pair, ui::Tab::Trains, 0, false, mock::Weather::Clear, -1, 0},
        {"first_run", mock::Scenario::FirstRun, ui::Screen::First, ui::Tab::Trains, 0, false, mock::Weather::Clear, -1, 0},
    };
    for (const Shot& s : shots) {
        static mock::Model m;
        m = mock::Model();
        m.scenario = s.sc;
        m.weather = s.w;
        mock::update(m, start + s.after, start);
        ui::App a;
        ui::init(a, 1000);
        a.view.screen = s.screen;
        a.view.tab = s.tab;
        a.scroll[static_cast<int>(s.tab)] = a.scrollTarget[static_cast<int>(s.tab)] = s.scroll;
        a.dim = s.dim;
        if (s.detailIdx >= 0) {
            const mock::Card& c = s.tab == ui::Tab::Trains ? m.trains[s.detailIdx] : m.buses[s.detailIdx];
            a.view.station = c.station;
            snprintf(a.view.route, sizeof(a.view.route), "%s", c.rec.route);
            snprintf(a.view.dir, sizeof(a.view.dir), "%s", c.rec.dir);
        }
        for (int pass = 0; pass < 2; ++pass) {   // twice: the home screen learns its hit areas on the first pass
            if (strips) drawStrips(fb, a, m, 1000, scratch);
            else ui::draw(cv, a, m, 1000, scratch);
        }
        writePng(out + "/" + s.name + ".png", fb, ui::brightness(a, m));
        printf("%s: mood %d, reason \"%s\"\n", s.name, static_cast<int>(m.mood), m.reason);
    }
    return 0;
}
