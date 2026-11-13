// Checks on a laptop that the device's frame shortcuts change no pixel the user would see (power analysis, round 2):
//   - the card cache (32 entries, each card rendered straight into the entry it replaces): every frame at rest is the
//     same as rendering every card fresh, through scrolling, tab switches, and minutes ticking over (a moving list
//     may use another cached width of a card, as it always has); and once both tabs' cards are cached, a tab switch
//     renders none while it slides (the 16 entries before didn't hold both tabs, so slides rendered cards);
//   - the dim always-on screen: two frames with the same ui::AodKey are the same pixels at the same brightness, so
//     skipping the second (main.cpp) shows nothing different;
//   - the Stops list at rest: redrawing only the live marks' windows when ui::restKey is unchanged leaves the panel
//     exactly as a whole frame would, and equal keys draw the same pixels outside the marks;
//   - the row hash (row_hash.h): a row that changes in any one word always hashes differently.
// Frames are drawn through the device's strip path (48-row strips in the panel's layout).
// Build and run (from firmware/handheld/mock):
//   c++ -std=c++17 -O2 -Isrc -I../../lib/gfx/src -I../../lib/cta/src -I../../lib/pet/src -I<ArduinoJson>/src tools/frame_test.cpp \
//       ../../lib/gfx/src/*.cpp src/ui.cpp src/mock_data.cpp ../../lib/cta/src/*.cpp ../../lib/pet/src/*.cpp \
//       -o /tmp/frame_test && /tmp/frame_test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "mock_data.h"
#include "row_hash.h"
#include "ui.h"

namespace {

int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                \
        }                                                              \
    } while (0)

constexpr uint32_t kStart = 1794019230;   // 2026-11-06 20:40:30 Chicago: the minute turns 30 s in

uint16_t scratch[ui::kScratchPixels];

// The frame through 48-row panel strips, as main.cpp draws it, hashed (FNV-1a 64 over every strip's pixels).
uint64_t frameHash(ui::App& a, const mock::Model& m, uint32_t ms) {
    constexpr int kRows = 48;
    static uint16_t strip[ui::W * kRows];
    uint64_t h = 1469598103934665603ull;
    for (int y0 = 0; y0 < ui::W; y0 += kRows) {
        const int rows = y0 + kRows <= ui::W ? kRows : ui::W - y0;
        ui::Canvas cv = ui::Canvas::panelBand(strip, ui::W, y0, rows);
        ui::draw(cv, a, m, ms, scratch);
        for (int i = 0; i < ui::W * rows; ++i) h = (h ^ strip[i]) * 1099511628211ull;
    }
    return h;
}

// ---------- the card cache ----------

struct Frame {
    uint64_t hash;
    bool still;       // at rest: each card at its exact width
    bool tap;         // the first frame after a tap on the other tab
    bool slide;       // a frame of the slide to the other tab
    uint32_t renders; // cards rendered for this frame
};

// A Stops list the way the stress test drives it: the auto-scroll, a rest on a card and a tap on the other tab every
// 5 s, and the data updated every second (frozen: never, so no card changes). Deterministic, so the same frames come
// out with or without a cache.
std::vector<Frame> scrollRun(bool frozen) {
    std::vector<Frame> out;
    static mock::Model m;
    m = mock::Model();
    m.scenario = mock::Scenario::Live;
    mock::update(m, kStart, kStart);
    ui::App a;
    ui::init(a, 1000);
    a.view.screen = ui::Screen::Stops;
    uint32_t lastData = 1000, nextTab = 6000, restUntil = 0;
    for (uint32_t ms = 1000; ms < 61000; ms += 30) {
        if (!frozen && ms - lastData >= 1000) {
            lastData = ms;
            mock::update(m, kStart + (ms - 1000) / 1000, kStart);
        }
        const int t = static_cast<int>(a.view.tab);
        bool tap = false;
        if (ms >= nextTab && !a.trans.on) {   // come to rest, then tap the other tab (as stress.cpp's scrollStep)
            nextTab = ms + 5000;
            a.scrollTarget[t] = ui::kRow * roundf(a.scrollTarget[t] / ui::kRow);
            a.scroll[t] = a.scrollTarget[t];
            const float x = a.view.tab == ui::Tab::Trains ? 350 : 116;
            ui::pointerDown(a, m, x, 50, ms - 40);
            ui::pointerUp(a, m, x, 50, ms);
            restUntil = ms + 1500;
            tap = true;
        } else if (ms >= restUntil && !a.trans.on) {
            a.scrollTarget[t] = 560 + 400 * sinf(ms / 700.f);
            a.lastInput = ms;
        } else {
            a.lastInput = ms;
        }
        ui::tick(a, m, ms);
        const uint32_t r0 = ui::cardRenders();
        const uint64_t h = frameHash(a, m, ms);
        out.push_back({h, !ui::animating(a, m, ms), tap, a.trans.on, ui::cardRenders() - r0});
    }
    return out;
}

void testCardCache() {
    ui::setCardCache(nullptr, 0);
    const std::vector<Frame> fresh = scrollRun(false);
    constexpr int kCards = 32;
    void* mem = malloc(ui::cardCacheBytes(kCards));
    ui::setCardCache(mem, kCards);
    const std::vector<Frame> cached = scrollRun(false);
    CHECK(fresh.size() == cached.size());
    int still = 0, same = 0;
    uint32_t rendersFresh = 0, rendersCached = 0;
    for (size_t i = 0; i < fresh.size() && i < cached.size(); ++i) {
        rendersFresh += fresh[i].renders;
        rendersCached += cached[i].renders;
        CHECK(fresh[i].still == cached[i].still);
        if (!fresh[i].still) continue;
        ++still;
        if (fresh[i].hash == cached[i].hash) ++same;
        else if (still - same <= 3) printf("FAIL: frame %zu at rest differs with the cache\n", i);
    }
    CHECK(still > 100);   // the run rests after every tab switch
    CHECK(same == still);
    printf("card cache: %d frames at rest, %d the same as rendering every card fresh; %u cards rendered (%u without "
           "the cache)\n", still, same, rendersCached, rendersFresh);

    // Cards that don't change: after the first visit to each tab, nothing is rendered while a tab switch slides
    // (its first frame is the one tap-to-screen times), and the whole run renders under a hundred cards (each card
    // at each width it comes to rest at, once).
    ui::setCardCache(nullptr, 0);
    ui::setCardCache(mem, kCards);
    const std::vector<Frame> frozen = scrollRun(true);
    int taps = 0, slideRenders = 0;
    uint32_t total = 0;
    for (const Frame& f : frozen) {
        total += f.renders;
        if (f.tap) ++taps;
        if (taps > 2 && (f.tap || f.slide)) slideRenders += f.renders;
    }
    CHECK(taps >= 10);
    CHECK(slideRenders == 0);
    CHECK(total < 120);   // 93 when written
    printf("card cache, cards unchanged: %d tab switches, %d cards rendered while sliding after both tabs were seen, "
           "%u in the whole run\n", taps, slideRenders, total);
    ui::setCardCache(nullptr, 0);
    free(mem);
}

// ---------- the always-on screen ----------

std::string keyString(const ui::AodKey& k) {
    char s[160];
    snprintf(s, sizeof(s), "%u|%u|%d|%d|%d|%d|%d|%d|%d|%s|%s|%p|%06x", k.screen, k.mood, k.dim, k.docked, k.clockKnown,
             k.stale, k.ox, k.oy, k.bob, k.clock, k.temp, static_cast<const void*>(k.icon), static_cast<unsigned>(k.rgb));
    return s;
}

void testAodKey() {
    struct State { mock::Scenario sc; mock::Weather w; bool clockKnown; bool live; bool hasWeather; float tempC; int wmo; };
    const State states[] = {
        {mock::Scenario::Live, mock::Weather::Clear, true, false, false, 0, 0},
        {mock::Scenario::Happy, mock::Weather::Rain, true, false, false, 0, 0},
        {mock::Scenario::Sad, mock::Weather::Snow, true, false, false, 0, 0},
        {mock::Scenario::Sleepy, mock::Weather::Clear, true, false, false, 0, 0},
        {mock::Scenario::Offline, mock::Weather::Clear, true, false, false, 0, 0},
        {mock::Scenario::Docked, mock::Weather::Clear, true, false, false, 0, 0},
        {mock::Scenario::Live, mock::Weather::Clear, false, true, false, 0, 0},     // live, before NTP and weather
        {mock::Scenario::Live, mock::Weather::Clear, true, true, true, 3.4f, 3},    // live weather, cloudy
        {mock::Scenario::Live, mock::Weather::Clear, true, true, true, 3.4f, 1},    // the same, clear: only the icon differs
        {mock::Scenario::Live, mock::Weather::Clear, true, true, true, -6.2f, 71},  // live weather, snow
    };
    std::map<std::string, std::pair<uint64_t, float>> seen;   // key -> frame and brightness
    std::vector<std::pair<std::string, ui::AodKey>> distinct;  // for checking operator==, which the device uses
    int frames = 0, skippable = 0, clashes = 0, wrongEq = 0;
    for (const State& st : states) {
        for (int screen = 0; screen < 2; ++screen) {
            uint64_t last = 0;
            std::string lastKey;
            for (uint32_t ms = 20000; ms < 95000; ms += 250) {   // 75 s: shifts, bobs, and a minute turning over
                static mock::Model m;
                m = mock::Model();
                m.scenario = st.sc;
                m.weather = st.w;
                mock::update(m, kStart + ms / 1000, kStart);
                m.clockKnown = st.clockKnown;
                m.live = st.live;
                m.hasWeather = st.hasWeather;
                m.tempC = st.tempC;
                m.wmo = st.wmo;
                ui::App a;
                ui::init(a, ms - 16000);
                a.view.screen = screen ? ui::Screen::First : ui::Screen::Home;
                ui::tick(a, m, ms);   // dims (16 s without input)
                CHECK(a.dim && ui::alwaysOn(a));
                const ui::AodKey k = ui::aodKey(a, m, ms);
                const std::string ks = keyString(k);
                const uint64_t h = frameHash(a, m, ms);
                const float b = ui::brightness(a, m);
                ++frames;
                for (const auto& d : distinct)   // operator== agrees with comparing every field
                    if ((d.first == ks) != (d.second == k) && ++wrongEq <= 3)
                        printf("FAIL: AodKey == disagrees: %s vs %s\n", d.first.c_str(), ks.c_str());
                auto it = seen.find(ks);
                if (it == seen.end()) {
                    seen[ks] = {h, b};
                    distinct.push_back({ks, k});
                }
                else if (it->second.first != h || it->second.second != b) {
                    if (++clashes <= 3) printf("FAIL: same always-on key, different frame: %s\n", ks.c_str());
                }
                if (!lastKey.empty() && ks == lastKey) {
                    ++skippable;
                    CHECK(h == last);
                }
                CHECK(ui::aodKey(a, m, ms) == k);
                last = h;
                lastKey = ks;
            }
        }
    }
    CHECK(clashes == 0);
    CHECK(wrongEq == 0);
    CHECK(skippable > frames / 3);   // about every other half-second frame repeats the one before
    printf("always-on key: %d frames, %zu distinct keys, %d would be skipped, %d clashes\n", frames, seen.size(), skippable,
           clashes);
}

// ---------- the list at rest: only the live marks redrawn ----------

// What the panel shows, in screen coordinates: a whole frame through the strips, or just the marks' windows (the
// device's path, main.cpp: each window even-aligned, drawn through Canvas::panelWindow).
struct Panel {
    uint16_t px[ui::W * ui::W];
    void full(ui::App& a, const mock::Model& m, uint32_t ms) {
        constexpr int kRows = 48;
        static uint16_t strip[ui::W * kRows];
        for (int y0 = 0; y0 < ui::W; y0 += kRows) {
            const int rows = y0 + kRows <= ui::W ? kRows : ui::W - y0;
            ui::Canvas cv = ui::Canvas::panelBand(strip, ui::W, y0, rows);
            ui::draw(cv, a, m, ms, scratch);
            for (int x = 0; x < ui::W; ++x)
                for (int y = y0; y < y0 + rows; ++y) px[y * ui::W + x] = strip[x * rows + (y0 + rows - 1 - y)];
        }
    }
    int marks(ui::App& a, const mock::Model& m, uint32_t ms, const ui::Mark* mk, int n) {
        int drawn = 0;
        for (int i = 0; i < n; ++i) {
            const int x0 = std::max(0, mk[i].x & ~1), x1 = std::min(ui::W, (mk[i].x + mk[i].w + 1) & ~1);
            const int y0 = std::max(0, mk[i].y & ~1), y1 = std::min(ui::W, (mk[i].y + mk[i].h + 1) & ~1);
            if (x0 >= x1 || y0 >= y1) continue;
            const int rows = y1 - y0;
            static uint16_t win[64 * 64];
            ui::Canvas cv = ui::Canvas::panelWindow(win, ui::W, x0, x1, y0, rows);
            ui::draw(cv, a, m, ms, scratch);
            for (int x = x0; x < x1; ++x)
                for (int y = y0; y < y1; ++y) px[y * ui::W + x] = win[(x - x0) * rows + (y0 + rows - 1 - y)];
            drawn += (x1 - x0) * rows;
        }
        return drawn;
    }
};

void testRestMarks() {
    constexpr int kCards = 32;
    void* mem = malloc(ui::cardCacheBytes(kCards));
    ui::setCardCache(mem, kCards);
    static Panel panel, truth;
    static mock::Model m;
    m = mock::Model();
    m.scenario = mock::Scenario::Live;
    mock::update(m, kStart, kStart);
    ui::App a;
    ui::init(a, 1000);
    a.view.screen = ui::Screen::Stops;
    uint32_t shown = 0, lastData = 1000;
    int frames = 0, markFrames = 0, wrong = 0, keyed = 0;
    size_t markPx = 0;
    // 6 minutes: rests at several scroll positions on both tabs, minutes ticking over, a spell offline (the stale
    // line), and long rests that dim the list (the burn-in shift) and then send it Home.
    for (uint32_t ms = 1000; ms < 361000; ms += 125) {
        if (ms - lastData >= 1000) {
            lastData = ms;
            m.scenario = ms > 200000 && ms < 230000 ? mock::Scenario::Offline : mock::Scenario::Live;
            mock::update(m, kStart + (ms - 1000) / 1000, kStart);
        }
        const uint32_t phase = (ms - 1000) % 60000;
        if (phase == 0) {   // a new minute: tap the other tab if on Stops, or open Stops from Home
            if (a.dim) { ui::pointerDown(a, m, 233, 30, ms - 40); ui::pointerUp(a, m, 233, 30, ms); }
            else if (a.view.screen == ui::Screen::Home) { ui::pointerDown(a, m, 233, 440, ms - 40); ui::pointerUp(a, m, 233, 440, ms); }
            else if (a.view.screen == ui::Screen::Stops) {
                const float x = a.view.tab == ui::Tab::Trains ? 350 : 116;
                ui::pointerDown(a, m, x, 50, ms - 40);
                ui::pointerUp(a, m, x, 50, ms);
            }
        } else if (phase == 1000 && a.view.screen == ui::Screen::Stops && !a.dim) {   // come to rest a few cards down
            const int t = static_cast<int>(a.view.tab);
            a.scrollTarget[t] = ui::kRow * static_cast<float>((ms / 60000) % 4);
            a.lastInput = ms;
        }
        if (phase == 500 && a.view.screen != ui::Screen::Stops && !a.dim) {   // the first tap only woke it
            ui::pointerDown(a, m, 233, 440, ms - 40);
            ui::pointerUp(a, m, 233, 440, ms);
        }
        ui::tick(a, m, ms);
        ui::Mark mk[8];
        int nm = 0;
        const uint32_t key = ui::restKey(a, m, ms, mk, 8, nm);
        if (key) ++keyed;
        if (key && key == shown) { markPx += panel.marks(a, m, ms, mk, nm); ++markFrames; }
        else panel.full(a, m, ms);
        shown = key;
        truth.full(a, m, ms);
        ++frames;
        if (memcmp(panel.px, truth.px, sizeof(panel.px)) != 0) {
            if (++wrong <= 3) printf("FAIL: frame at %u ms differs after redrawing only the marks\n", ms);
            memcpy(panel.px, truth.px, sizeof(panel.px));
        }
        CHECK(ui::restKey(a, m, ms, mk, 8, nm) == key);
    }
    CHECK(wrong == 0);
    CHECK(markFrames > frames / 4);

    // Equal keys, the same frame outside the marks (and inside them too at the same blink phase), over separate
    // states at rest: each tab and scroll position, awake and dim (its burn-in shift), minutes ticking, and offline.
    std::map<uint32_t, uint64_t> outside;
    std::map<std::pair<uint32_t, int>, uint64_t> whole;
    int samples = 0, clashes = 0, repeats = 0;
    for (int off = 0; off < 2; ++off)
        for (int t = 0; t < 2; ++t)
            for (int j = 0; j < 5; ++j)
                for (int dim = 0; dim < 2; ++dim)
                    for (uint32_t ms = 20000; ms < 140000; ms += 2500) {
                        m = mock::Model();
                        m.scenario = off ? mock::Scenario::Offline : mock::Scenario::Live;
                        mock::update(m, kStart + ms / 1000, kStart);
                        ui::App b;
                        ui::init(b, dim ? ms - 16000 : ms);
                        b.view.screen = ui::Screen::Stops;
                        b.view.tab = t ? ui::Tab::Buses : ui::Tab::Trains;
                        b.scroll[t] = b.scrollTarget[t] = ui::kRow * static_cast<float>(j);
                        ui::tick(b, m, ms);
                        ui::Mark mk[8];
                        int nm = 0;
                        const uint32_t key = ui::restKey(b, m, ms, mk, 8, nm);
                        CHECK(key != 0);
                        CHECK(b.dim == (dim != 0));
                        truth.full(b, m, ms);
                        uint64_t hw = 1469598103934665603ull;
                        for (uint16_t v : truth.px) hw = (hw ^ v) * 1099511628211ull;
                        for (int i = 0; i < nm; ++i) {   // blank the marks' windows, as the device draws them
                            const int x0 = std::max(0, mk[i].x & ~1), x1 = std::min(ui::W, (mk[i].x + mk[i].w + 1) & ~1);
                            const int y0 = std::max(0, mk[i].y & ~1), y1 = std::min(ui::W, (mk[i].y + mk[i].h + 1) & ~1);
                            for (int y = y0; y < y1; ++y)
                                for (int x = x0; x < x1; ++x) truth.px[y * ui::W + x] = 0;
                        }
                        uint64_t ho = 1469598103934665603ull;
                        for (uint16_t v : truth.px) ho = (ho ^ v) * 1099511628211ull;
                        ++samples;
                        auto o = outside.emplace(key, ho);
                        if (!o.second) { ++repeats; if (o.first->second != ho && ++clashes <= 3) printf("FAIL: same rest key, different frame at %u ms\n", ms); }
                        auto w = whole.emplace(std::make_pair(key, static_cast<int>((ms / 500) % 2)), hw);
                        if (!w.second && w.first->second != hw && ++clashes <= 3) printf("FAIL: same rest key and blink, different frame\n");
                    }
    CHECK(clashes == 0);
    CHECK(repeats > samples / 4);
    printf("rest key: %d states, %zu distinct keys, %d repeats, %d clashes\n", samples, outside.size(), repeats, clashes);
    printf("list at rest: %d frames, %d keyed, %d drew only the marks (%zu px each on average), %d differ\n", frames,
           keyed, markFrames, markFrames ? markPx / markFrames : 0, wrong);
    ui::setCardCache(nullptr, 0);
    free(mem);
}

// ---------- the row hash ----------

uint32_t rnd() {
    static uint32_t s = 12345;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

void testRowHash() {
    int flips = 0, missed = 0;
    for (int n : {48, 34, 20}) {
        for (int trial = 0; trial < 2000; ++trial) {
            uint32_t w[24];
            for (uint32_t& x : w) x = rnd();
            if (trial % 3 == 0) for (int i = 0; i < n / 2; ++i) w[i] = 0;   // black rows are common
            const uint32_t h = hashRow(reinterpret_cast<const uint16_t*>(w), n);
            for (int i = 0; i < n / 2; ++i) {
                uint32_t v[24];
                memcpy(v, w, sizeof(v));
                uint32_t d = rnd();
                if (trial & 1) d &= 0xFFFF << ((rnd() & 1) * 16);   // one pixel
                if (!d) d = 1;
                v[i] ^= d;
                ++flips;
                if (hashRow(reinterpret_cast<const uint16_t*>(v), n) == h) ++missed;
            }
        }
    }
    CHECK(missed == 0);
    printf("row hash: %d one-word changes, %d missed\n", flips, missed);
}

}  // namespace

int main() {
    testRowHash();
    testAodKey();
    testCardCache();
    testRestMarks();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
