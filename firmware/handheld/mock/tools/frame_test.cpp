// Checks on a laptop that the device's frame shortcuts change no pixel the user would see (power analysis, round 2):
//   - the card cache (32 entries, each card rendered straight into the entry it replaces): every frame at rest is the
//     same as rendering every card fresh, through scrolling, tab switches, and minutes ticking over (a moving list
//     may use another cached width of a card, as it always has); and once both tabs' cards are cached, a tab switch
//     renders none while it slides (the 16 entries before didn't hold both tabs, so slides rendered cards);
//   - the dim always-on screen: two frames with the same ui::AodKey are the same pixels at the same brightness, so
//     skipping the second (main.cpp) shows nothing different;
//   - the row hash (row_hash.h): a row that changes in any one word always hashes differently;
//   - the changed rows (row_hash.h's changedRuns, run on the sender since round 3): the same runs as the loop that
//     found them on the drawing core before, and sending just those leaves the panel showing every frame;
//   - the sender's strip buffers (strip_send.h): against a model of the loop and the DMA, interleaved at random, a
//     buffer is never drawn over while the sender has it or the DMA reads it, always comes back, and nothing
//     deadlocks.
// Frames are drawn through the device's strip path (48-row strips in the panel's layout).
// Build and run (from firmware/handheld/mock):
//   c++ -std=c++17 -O2 -Isrc -I../../lib/cta/src -I../../lib/pet/src -I<ArduinoJson>/src tools/frame_test.cpp \
//       src/gfx2d.cpp src/fonts.cpp src/ui.cpp src/mock_data.cpp ../../lib/cta/src/*.cpp ../../lib/pet/src/*.cpp \
//       -o /tmp/frame_test && /tmp/frame_test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "mock_data.h"
#include "row_hash.h"
#include "strip_send.h"
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

// ---------- the changed rows (row_hash.h's changedRuns, which the sender runs since round 3) ----------

// The loop that found the changed rows on the drawing core before round 3 (main.cpp's drawFrame), for comparison.
void oldRuns(const uint16_t* buf, int rows, uint32_t* hashes, bool all, std::vector<std::pair<int, int>>& out) {
    int runStart = -1, runEnd = -1;
    auto flush = [&]() {
        if (runStart < 0) return;
        out.push_back({runStart & ~1, runEnd | 1});
        runStart = -1;
    };
    for (int x = 0; x < ui::W; ++x) {
        const uint32_t h = hashRow(buf + x * rows, rows);
        if (h == hashes[x] && !all) continue;
        hashes[x] = h;
        if (runStart >= 0 && x - runEnd > 8) flush();
        if (runStart < 0) runStart = x;
        runEnd = x;
    }
    flush();
}

// Frames through the strips as the device draws them: a Stops list scrolling with tab switches, coming to rest,
// dimming, and the always-on screen, with every row resent once midway (as after the "p" profile). Each strip's runs
// must be the old loop's, and copying just those rows onto a model of the panel must leave it showing the frame.
void testChangedRuns() {
    constexpr int kRows = 48, kStrips = (ui::W + kRows - 1) / kRows;
    static uint16_t strip[ui::W * kRows];
    static uint16_t panel[kStrips][ui::W * kRows];   // what the panel shows, strip by strip, in the strips' layout
    static uint32_t hNew[kStrips][ui::W], hOld[kStrips][ui::W];
    memset(hNew, 0, sizeof(hNew));
    memset(hOld, 0, sizeof(hOld));
    static mock::Model m;
    m = mock::Model();
    m.scenario = mock::Scenario::Live;
    mock::update(m, kStart, kStart);
    ui::App a;
    ui::init(a, 1000);
    a.view.screen = ui::Screen::Stops;
    int frames = 0, runs = 0, wrongRuns = 0, wrongPanel = 0, aodFrames = 0;
    long rowsSent = 0;
    uint32_t lastData = 1000;
    for (uint32_t ms = 1000; ms < 70000; ms += 30) {
        if (ms - lastData >= 1000) {
            lastData = ms;
            mock::update(m, kStart + (ms - 1000) / 1000, kStart);
        }
        const int t = static_cast<int>(a.view.tab);
        if (ms < 20000 && !a.trans.on) {   // scrolling, with a tap on the other tab every 6 s
            if (ms % 6000 < 30) {
                const float x = a.view.tab == ui::Tab::Trains ? 350 : 116;
                ui::pointerDown(a, m, x, 50, ms - 40);
                ui::pointerUp(a, m, x, 50, ms);
            } else {
                a.scrollTarget[t] = 560 + 400 * sinf(ms / 700.f);
                a.lastInput = ms;
            }
        } else if (ms < 20030) {
            a.scrollTarget[t] = ui::kRow * roundf(a.scrollTarget[t] / ui::kRow);   // then no input: rest, dim, Home
        }
        ui::tick(a, m, ms);
        const bool all = frames == 0 || ms == 25000;
        for (int s = 0; s < kStrips; ++s) {
            const int y0 = s * kRows, rows = y0 + kRows <= ui::W ? kRows : ui::W - y0;
            ui::Canvas cv = ui::Canvas::panelBand(strip, ui::W, y0, rows);
            ui::draw(cv, a, m, ms, scratch);
            std::vector<std::pair<int, int>> got, want;
            changedRuns(strip, rows, ui::W, hNew[s], all, 8, [&](int r0, int r1) {
                got.push_back({r0, r1});
                memcpy(panel[s] + r0 * rows, strip + r0 * rows, static_cast<size_t>(r1 - r0 + 1) * rows * 2);
                rowsSent += r1 - r0 + 1;
            });
            oldRuns(strip, rows, hOld[s], all, want);
            runs += static_cast<int>(got.size());
            if (got != want && ++wrongRuns <= 3) printf("FAIL: strip %d at %u ms: runs differ from the old loop\n", s, ms);
            if (memcmp(panel[s], strip, static_cast<size_t>(ui::W) * rows * 2) != 0 && ++wrongPanel <= 3)
                printf("FAIL: strip %d at %u ms: the panel doesn't show the frame\n", s, ms);
        }
        ++frames;
        if (ui::alwaysOn(a)) ++aodFrames;
    }
    CHECK(wrongRuns == 0);
    CHECK(wrongPanel == 0);
    CHECK(aodFrames > 100);
    printf("changed rows: %d frames (%d always-on), %d runs, %ld panel rows sent of %d, %d differ from the old loop, %d "
           "frames wrong on the panel\n", frames, aodFrames, runs, rowsSent, frames * kStrips * ui::W, wrongRuns,
           wrongPanel);
}

// ---------- the sender's buffers (strip_send.h's StripSender, as main.cpp's sendTask runs it) ----------

// The loop, the sender and the DMA as three actors sharing the two strip buffers, interleaved at random (the DMA
// finishes a send whenever it gets a turn, or when the sender waits for it). The loop draws each strip into buffer
// strip & 1 once it's back, queues it, ends each frame with an end-of-frame job, and every few frames takes both
// buffers, as the "p" profile does, then has the next frame send every row. Checks: the loop never draws into a buffer
// the sender has or the DMA still reads; each buffer comes back exactly once each time the loop hands it over;
// nothing deadlocks; and when the sender ends a frame, the panel shows that frame.
namespace sendsim {

constexpr int kRows = 8, kN = 64, kStrips = 5, kFrames = 40, kGap = 3;
enum class Owner : uint8_t { Free, Loop, Sender };
Owner owner[2];
uint16_t buf[2][kRows * kN];
uint16_t panel[kStrips][kRows * kN];
uint32_t hashes[kStrips][kN];
struct { bool on; int b, strip, r0, r1; } dma;
struct Job { int frame, strip, b; bool all, end; };
std::vector<Job> queue;   // FIFO
int violations = 0;
uint32_t rng = 1;

uint32_t next() {
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

void violation(const char* what, int seed) {
    if (++violations <= 5) printf("FAIL: sender buffers, seed %d: %s\n", seed, what);
}

// A strip's pixels in a frame: rows change at different rates, some never, and every 4th frame the even strips don't
// change at all (a strip with nothing to send).
uint16_t pixel(int f, int s, int x, int i) {
    if (f % 4 == 3 && s % 2 == 0) --f;
    const int period = 1 + (x * 7 + s) % 5;
    const uint32_t epoch = x % 9 == 0 ? 0 : static_cast<uint32_t>(f / period);
    uint32_t h = (epoch * 2654435761u) ^ (static_cast<uint32_t>(s) * 40503u) ^ (static_cast<uint32_t>(x) << 16) ^
                 static_cast<uint32_t>(i);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return static_cast<uint16_t>(h ^ (h >> 15));
}

void completeDma() {
    if (!dma.on) return;
    memcpy(panel[dma.strip] + dma.r0 * kRows, buf[dma.b] + dma.r0 * kRows,
           static_cast<size_t>(dma.r1 - dma.r0 + 1) * kRows * 2);
    dma.on = false;
}

void run(int seed) {
    rng = static_cast<uint32_t>(seed) * 2654435761u + 1;
    owner[0] = owner[1] = Owner::Free;
    memset(panel, 0, sizeof(panel));
    memset(hashes, 0, sizeof(hashes));
    dma.on = false;
    queue.clear();
    auto finish = []() { completeDma(); };
    auto give = [seed](int b) {
        if (owner[b] != Owner::Sender) violation("a buffer given back that the sender didn't have", seed);
        if (dma.on && dma.b == b) violation("a buffer given back while the DMA still reads it", seed);
        owner[b] = Owner::Free;
    };
    StripSender out(finish, give);
    // The loop's program: each frame's strips, its end, and every 7th frame a profile (both buffers taken, then given).
    struct Step { int kind, frame, strip; };   // 0 draw, 1 end, 2 take buffer `strip`, 3 give both back
    std::vector<Step> prog;
    for (int f = 0; f < kFrames; ++f) {
        for (int s = 0; s < kStrips; ++s) prog.push_back({0, f, s});
        prog.push_back({1, f, 0});
        if (f % 7 == 6) prog.insert(prog.end(), {{2, f, 0}, {2, f, 1}, {3, f, 0}});
    }
    size_t pc = 0;
    bool sendAll = true, check = true;   // check: the sender looks at the queue before it waits
    int doneFrames = 0;
    for (int guard = 0; guard < 1000000; ++guard) {
        const bool loopDone = pc == prog.size();
        bool loopCan = false;
        if (!loopDone) {
            const Step& st = prog[pc];
            loopCan = st.kind == 0 ? owner[st.strip & 1] == Owner::Free : st.kind == 2 ? owner[st.strip] == Owner::Free : true;
        }
        const bool senderCan = check || !queue.empty();
        if (loopDone && queue.empty() && !dma.on) break;
        if (!loopCan && !senderCan && !dma.on) {
            violation("deadlock: the loop waits for a buffer and the sender for a job", seed);
            return;
        }
        const uint32_t pick = next() % 3;
        if (pick == 0 && loopCan) {
            const Step st = prog[pc++];
            if (st.kind == 0) {
                const int b = st.strip & 1;
                if (dma.on && dma.b == b) violation("the loop drew into a buffer the DMA still reads", seed);
                owner[b] = Owner::Loop;
                for (int x = 0; x < kN; ++x)
                    for (int i = 0; i < kRows; ++i) buf[b][x * kRows + i] = pixel(st.frame, st.strip, x, i);
                owner[b] = Owner::Sender;
                queue.push_back({st.frame, st.strip, b, sendAll, false});
            } else if (st.kind == 1) {
                sendAll = false;
                queue.push_back({st.frame, 0, -1, false, true});
            } else if (st.kind == 2) {
                owner[st.strip] = Owner::Loop;
            } else {
                owner[0] = owner[1] = Owner::Free;
                sendAll = true;
            }
        } else if (pick == 1 && senderCan) {
            if (check) {
                out.beforeWait(!queue.empty());
                check = false;
                continue;
            }
            const Job j = queue.front();
            queue.erase(queue.begin());
            check = true;
            if (j.end) {
                out.frameEnd();
                if (dma.on) violation("a frame ended with a send still going out", seed);
                for (int s = 0; s < kStrips; ++s)
                    for (int x = 0; x < kN; ++x)
                        for (int i = 0; i < kRows; ++i)
                            if (panel[s][x * kRows + i] != pixel(j.frame, s, x, i)) {
                                violation("the panel doesn't show the frame at its end", seed);
                                s = kStrips;
                                x = kN;
                                break;
                            }
                ++doneFrames;
                continue;
            }
            if (owner[j.b] != Owner::Sender) violation("the sender got a buffer it doesn't have", seed);
            out.strip(buf[j.b], kRows, kN, hashes[j.strip], j.all, kGap, j.b, [&](int r0, int r1) {
                if (dma.on) violation("a send started before the one before it was done", seed);
                dma = {true, j.b, j.strip, r0, r1};
            });
        } else if (pick == 2) {
            completeDma();
        }
    }
    if (doneFrames != kFrames) violation("not every frame ended", seed);
    if (owner[0] != Owner::Free || owner[1] != Owner::Free) violation("a buffer never came back", seed);
}

}  // namespace sendsim

void testStripSender() {
    constexpr int kSeeds = 400;
    for (int seed = 0; seed < kSeeds; ++seed) sendsim::run(seed);
    CHECK(sendsim::violations == 0);
    printf("sender buffers: %d random interleavings of %d frames, %d problems\n", kSeeds, sendsim::kFrames,
           sendsim::violations);
}

}  // namespace

int main() {
    testRowHash();
    testChangedRuns();
    testStripSender();
    testAodKey();
    testCardCache();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
