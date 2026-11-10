#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "alerts_weather.h"
#include "cta_config.h"
#include "fonts.h"
#include "line_colors.h"
#include "mood.h"
#include "pet_render.h"
#include "pet_sprites.h"

namespace ui {

namespace {

using mock::Card;
using mock::Model;
using mock::Scenario;

constexpr int CARD_H = 96, ROW = kRow, FOCUS = 206;
constexpr uint32_t kBusCard = 0x2F7BFF;   // the app's bus blue for cards (iOS redesign spec); the LEDs use cta::kBusColor
constexpr uint16_t kKey = 0x0001;         // transparent in the card scratch buffer

const Font* F(int w, int s) { return Canvas::font(w, s); }

// ---------- line colors: a port of the app's LineColors card math ----------
double lum(uint32_t rgb) {
    auto ch = [&](int s) { const double c = ((rgb >> s) & 255) / 255.0; return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(16) + 0.7152 * ch(8) + 0.0722 * ch(0);
}
double contrast(uint32_t a, uint32_t b) { const double x = lum(a), y = lum(b); return (fmax(x, y) + 0.05) / (fmin(x, y) + 0.05); }
uint32_t shaded(uint32_t rgb, double target, double cap) {
    const double f = fmin(cap, pow(target / lum(rgb), 1 / 2.2));
    auto s = [&](int sh) { return static_cast<uint32_t>(lround(((rgb >> sh) & 255) * f)) << sh; };
    return s(16) | s(8) | s(0);
}
uint32_t mix(uint32_t rgb, double k) {
    auto ch = [&](int sh) { const double v = (rgb >> sh) & 255; return static_cast<uint32_t>(lround(v + (255 - v) * k)) << sh; };
    return ch(16) | ch(8) | ch(0);
}
uint32_t lifted(uint32_t rgb, uint32_t bg, double min, int from) {
    for (int s = from; s <= 10; ++s) { const uint32_t m = mix(rgb, s / 10.0); if (contrast(m, bg) >= min) return m; }
    return 0xFFFFFF;
}
struct Theme { char route[8]; uint32_t accent, top, card, text; };
const Theme& th(const char* route, bool bus) {
    static Theme cache[32];
    static int n = 0;
    for (int i = 0; i < n; ++i)
        if (strcmp(cache[i].route, route) == 0) return cache[i];
    Theme& t = cache[n < 32 ? n++ : 31];
    snprintf(t.route, sizeof(t.route), "%s", route);
    const uint32_t a = bus ? kBusCard : cta::lineColor(route);
    t.accent = a;
    t.top = shaded(a, 0.09, 0.95);
    t.card = shaded(a, 0.04, 0.85);
    t.text = lifted(a, t.top, 4.5, 3);
    return t;
}

// ---------- glyphs ----------
const char* const kLive[2][8] = {{".......K", ".....K..", "..KK.K.K", ".KKKK.K.", ".KKKK.K.", "..KK.K.K", ".....K..", ".......K"},
                                 {"........", "........", "..KK....", ".KKKK...", ".KKKK...", "..KK....", "........", "........"}};
const char* const kAlert[16] = {".......KK.......", ".......KK.......", "......KKKK......", "......KKKK......", ".....KKKKKK.....",
                                ".....KK..KK.....", "....KKK..KKK....", "....KKK..KKK....", "...KKKK..KKKK...", "...KKKK..KKKK...",
                                "..KKKKKKKKKKKK..", "..KKKKKKKKKKKK..", ".KKKKKK..KKKKKK.", ".KKKKKK..KKKKKK.", "KKKKKKKKKKKKKKKK",
                                "KKKKKKKKKKKKKKKK"};
struct Icon { const char* name; const char* rows[7]; int n; };
const Icon kIcons[] = {
    {"sun", {"..x..", "x.x.x", ".xxx.", "xxxxx", ".xxx.", "x.x.x", "..x.."}, 7},
    {"cloud", {".......", "..xx...", ".xxxxx.", "xxxxxxx", ".......", ".x.x.x.", "x.x.x.."}, 7},
    {"snow", {"x.x.x", ".xxx.", "xx.xx", ".xxx.", "x.x.x"}, 5},
    {"wifi", {".xxxxx.", "x.....x", "..xxx..", ".x...x.", "...x..."}, 5},
    {"phone", {"xxx", "x.x", "x.x", "x.x", "xxx"}, 5},
    {"off", {"x...x", ".x.x.", "..x..", ".x.x.", "x...x"}, 5},
};
void icon(Canvas& cv, const char* name, float cx, float cy, int s, uint32_t rgb) {
    for (const Icon& ic : kIcons)
        if (strcmp(ic.name, name) == 0) {
            const int w = static_cast<int>(strlen(ic.rows[0])), h = ic.n;
            cv.ink(ic.rows, h, static_cast<int>(lroundf(cx - w * s / 2.f)), static_cast<int>(lroundf(cy - h * s / 2.f)), s, rgb);
        }
}
void liveGlyph(Canvas& cv, int x, int y, uint32_t rgb, bool blinks, uint32_t ms) {
    cv.ink(kLive[blinks && (ms / 500) % 2 ? 1 : 0], 8, x, y, 2, rgb);
}
float chordHalf(float y) { const float d = fabsf(y - C); return d >= C ? 0 : sqrtf(C * C - d * d); }

void chevron(Canvas& cv, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb) {
    cv.line(x0, y0, x1, y1, 3, rgb);
    cv.line(x1, y1, x2, y2, 3, rgb);
}

// ---------- Trackie, via the shared pet renderer ----------
// wx: the weather for his umbrella or scarf; nullptr for none.
void drawPet(Canvas& cv, cta::Mood mood, bool docked, int x, int y, int s, const mock::WeatherView* wx, bool extra) {
    const pet::PetState st = pet::petStateFor(mood, docked);
    auto put = [&](const pet::Sprite& sp) { cv.sprite(sp.idx, sp.w, sp.h, sp.dx, sp.dy, x, y, pet::kDefaultPalette565, s); };
    put(pet::kPet[static_cast<int>(st)]);
    if (wx)
        if (const pet::Sprite* ov = pet::overlayFor(st, cta::accessoryFor(wx->wmo, wx->tempC))) put(*ov);
    if (extra && pet::kPetExtra[static_cast<int>(st)]) put(*pet::kPetExtra[static_cast<int>(st)]);
}

// ---------- the route card ----------
struct CardView {
    const char* route;
    bool bus;
    const char* heading;
    const char* stop;
    int min;
    bool sched;
    char clock[8];
    bool delayed, alert, stale;
    const char* pips[3];
    int nPips;
};

CardView viewOf(const Card& c, bool bus, const Model& m) {
    CardView v{};
    v.route = c.rec.route;
    v.bus = bus;
    v.heading = c.heading;
    v.stop = c.rec.name;
    v.min = c.rec.arrivals[0].min;
    v.sched = c.sched;
    mock::clockText(m.now, v.min, v.clock, sizeof(v.clock));
    v.delayed = c.rec.arrivals[0].delayed;
    v.alert = c.rec.alert;
    v.stale = cta::isStale(c.rec, m.now);
    for (int i = 0; i < c.nPips; ++i) v.pips[i] = c.pips[i];
    v.nPips = c.nPips;
    return v;
}

void routeCard(Canvas& cv, int x, int y, int w, int h, const CardView& c, uint32_t ms, bool glyph = true) {
    const Theme& t = th(c.route, c.bus);
    const float saved = cv.alpha;
    if (c.stale) cv.alpha *= 0.55f;
    PlateStyle ps;
    ps.fill = t.top; ps.fill2 = t.card; ps.rim = t.text; ps.rimAlpha = 0.25f;
    cv.plate(x, y, w, h, ps);
    char buf[48];
    const int R = x + w - 14;
    int rightW;
    if (c.sched) rightW = cv.pixelText(c.clock, R, y + 34, 3, t.text, Align::Right);
    else {
        const bool due = c.min <= 1;
        const int s = due ? 4 : 5;
        char big[8];
        snprintf(big, sizeof(big), due ? "Due" : "%d", c.min);
        const int dw = Canvas::pixelWidth(big, s), dx = R - 20 - dw;
        cv.pixelText(big, dx, y + (due ? 28 : 16), s, t.text);
        if (glyph || c.stale) liveGlyph(cv, R - 16, y + 16, t.text, !c.stale, ms);
        if (!due) cv.text(F(800, 12), "minutes", dx + dw / 2.f, y + 58, t.text, Align::Center);
        rightW = dw + 20;
    }
    const int L = x + 16;
    const float maxW = w - 32 - rightW - 10;
    const Font* fName = F(900, 34);
    const char* name = Canvas::fit(fName, c.bus ? c.route : mock::lineName(c.route), maxW - (c.alert ? 26 : 0), buf, sizeof(buf));
    const float nw = cv.text(fName, name, L, y + 4, t.text);
    if (c.alert) cv.ink(kAlert, 16, static_cast<int>(L + nw + 8), y + 16, 1, t.text);
    cv.arrowCircle(L + 8, y + 59, 8, t.text, t.card);
    const Font* fHead = F(900, 16);
    cv.text(fHead, Canvas::fit(fHead, c.heading, maxW - 22, buf, sizeof(buf)), L + 22, y + 49, t.text);
    int sx = L;
    if (c.delayed) sx += cv.chip("Delayed", L, y + 72, col::amber, 0x000000) + 6;
    const Font* fStop = F(800, 13);
    const float pipW = c.nPips * 14;
    const char* stop = Canvas::fit(fStop, c.stop, maxW + rightW - (sx - L) - pipW - 4, buf, sizeof(buf));
    const float sw = cv.text(fStop, stop, sx, y + 73, t.text);
    float px = sx + sw + 8;
    for (int i = 0; i < c.nPips; ++i) {
        const Theme& pt = th(c.pips[i], false);
        cv.fillRect(px, y + 76, 10, 10, t.text);
        cv.fillRect(px + 1.5f, y + 77.5f, 7, 7, pt.accent);
        px += 14;
    }
    cv.alpha = saved;
}

// ---------- shared pieces ----------
const char* moodWord(const Model& m) {
    if (m.docked) return "Zzz\xE2\x80\xA6";
    switch (m.mood) {
        case cta::Mood::Happy: return "All on time";
        case cta::Mood::Hyped: return "Go now!";
        case cta::Mood::Worried: return "Running late";
        case cta::Mood::Sad: return "Uh oh\xE2\x80\xA6";
        default: return "Sleepy\xE2\x80\xA6";
    }
}
bool anyStale(const Model& m) { return m.scenario == Scenario::Offline || m.offline; }
// The offline line under the status row: how old the data is, or what the network is doing.
void staleText(const Model& m, char* out, size_t cap) {
    const Card* c = m.nTrains ? &m.trains[0] : m.nBuses ? &m.buses[0] : nullptr;
    const uint32_t ts = m.live ? m.fetchedAt : c ? c->rec.ts : 0;   // live: the newest fetch
    if (ts && m.now >= ts) snprintf(out, cap, "Updated %d min ago", static_cast<int>((m.now - ts) / 60));
    else snprintf(out, cap, "%s", m.status[0] ? m.status : "No data yet");
}
const char* bubbleText(const App& a, const Model& m, uint32_t ms) {
    if (a.view.screen == Screen::First) return "Hi! I\xE2\x80\x99m Trackie";
    const bool poked = a.poked && ms - a.pokeT0 < 2600;
    if (anyStale(m) && !m.docked) return poked ? m.reason : "No signal";
    return poked ? m.reason : moodWord(m);
}

void drawStatusRow(Canvas& cv, const Model& m, int y, bool dim) {
    char tm[8];
    if (m.clockKnown) mock::clockText(m.now, 0, tm, sizeof(tm));
    else snprintf(tm, sizeof(tm), "--:--");
    const int tw = Canvas::pixelWidth(tm, 4);
    cv.pixelText(tm, C, y - 14, 4, dim ? col::mist : col::white, Align::Center);
    const mock::WeatherView wx = mock::weatherView(m);
    if (wx.icon) {
        icon(cv, wx.icon, C + tw / 2.f + 24, y, 3, dim ? col::dimInk : wx.rgb);
        cv.text(F(800, 18), wx.temp, C + tw / 2.f + 38, y + 1, dim ? col::dimInk : col::mist, Align::Left, Base::Middle);
    }
    const bool stale = anyStale(m);
    icon(cv, stale ? "off" : "wifi", C - tw / 2.f - 24, y, 3, stale ? col::amber : col::dimInk);
}

float bubbleWidth(const char* text) { return ceilf(fminf(170, Canvas::measure(F(900, 20), text))) + 26; }

void bubble(Canvas& cv, const char* text, int x, float cy, const char* label = "TRACKIE") {
    const int w = static_cast<int>(bubbleWidth(text)), h = 56, y = static_cast<int>(lroundf(cy - h / 2.f));
    PlateStyle ps = smallPlate();
    ps.fill = ps.fill2 = col::cream; ps.rim = col::navy; ps.rimAlpha = 1; ps.rimW = 2; ps.edge = 4;
    cv.plate(x, y, w, h, ps);
    cv.fillRect(x - 6, y + h / 2 - 6, 10, 4, col::cream);
    cv.fillRect(x - 2, y + h / 2 - 8, 6, 12, col::cream);
    cv.fillRect(x - 6, y + h / 2 - 2, 4, 4, col::cream);
    cv.text(F(900, 11), label, x + 13, y + 8, col::navy, Align::Left, Base::Top, 0.6f);
    char buf[48];
    cv.text(F(900, 20), Canvas::fit(F(900, 20), text, 170, buf, sizeof(buf)), x + 13, y + 22, col::navy);
}

struct Rect { int x, y, w, h; };
bool inRect(float x, float y, const Rect& r) { return x > r.x && x < r.x + r.w && y > r.y && y < r.y + r.h; }
Rect gPetTile{0, 0, 0, 0}, gHomeCard{0, 0, 0, 0};

// ---------- screens ----------
void drawHome(Canvas& cv, const App& a, const Model& m, uint32_t ms, bool aod, const View& v) {
    const bool docked = m.docked, first = v.screen == Screen::First, stale = anyStale(m);
    drawStatusRow(cv, m, 68, aod || docked);
    const bool up = (ms / 500) % 2 == 0;
    if (aod) { drawPet(cv, m.mood, docked, C - 64, 146 + ((ms / 1000) % 2 ? 0 : -4), 4, nullptr, false); return; }
    if (!docked && m.weather != mock::Weather::Clear) {   // rain or snow behind Trackie
        const bool rain = m.weather == mock::Weather::Rain;
        for (int i = 0; i < 16; ++i) {
            const int x = (i * 97 + 31) % 340 + 63;
            const int y = 100 + static_cast<int>(fmodf(ms * (rain ? 0.12f : 0.03f) + i * 53, 190));
            if (rain) cv.fillRect(x - x % 4, y - y % 4, 4, 12, 0x24427a);
            else cv.fillRect(x - x % 4 + static_cast<int>(lroundf(sinf(ms / 600.f + i) * 2)) * 4, y - y % 4, 4, 4, 0xc9d4ea);
        }
    }
    int hop = 0;
    if (a.poked) { const float k = (ms - a.pokeT0) / 360.f; if (k < 1) hop = static_cast<int>(lroundf(sinf(k * 3.14159f) * 3)); }
    const char* text = bubbleText(a, m, ms);
    const int tile = 152, gap = 20;
    const float total = tile + gap + bubbleWidth(text);
    const int tx = static_cast<int>(lroundf(C - total / 2)), ty = 108;
    gPetTile = {tx, ty, tile, tile};
    const mock::WeatherView wx = mock::weatherView(m);
    drawPet(cv, m.mood, docked, tx + 12, ty + 12 + (up ? -4 : 0) - hop * 4, 4, &wx, true);
    bubble(cv, text, tx + tile + gap, ty + tile / 2.f);
    gHomeCard = {0, 0, 0, 0};
    if (docked) {
        cv.arc(C, C, 226, 0, 6.2832f, 5, 0x123d2a);
        char charge[32];
        if (m.battery < 0) snprintf(charge, sizeof(charge), "On USB power");
        else snprintf(charge, sizeof(charge), m.charging ? "Charging \xC2\xB7 %d%%" : "Charged \xC2\xB7 %d%%", m.battery);
        if (m.battery >= 0) cv.arc(C, C, 226, -1.5708f, -1.5708f + 6.2832f * m.battery / 100.f, 5, col::signal);
        cv.text(F(900, 24), charge, C, 296, col::signal, Align::Center);
        cv.text(F(800, 16), "Resting on the dock", C, 330, col::mist, Align::Center);
        return;
    }
    if (first) {
        cv.plate(C - 150, 292, 300, 76, PlateStyle());
        cv.text(F(900, 19), "Open Trackie on your iPhone", C, 306, col::white, Align::Center);
        cv.text(F(800, 15), "to find stops near you", C, 334, col::mist, Align::Center);
        return;
    }
    if (m.glance) {
        gHomeCard = {C - 160, 290, 320, CARD_H};
        routeCard(cv, C - 160, 290, 320, CARD_H, viewOf(*m.glance, m.glanceIsBus, m), ms);
    }
    if (v.screen != Screen::Home) return;
    chevron(cv, 436, C - 12, 446, C, 436, C + 12, col::mist);   // swipe left: the map
    if (stale) {
        char s[40];
        staleText(m, s, sizeof(s));
        cv.text(F(800, 14), s, C, 406, col::amber, Align::Center);
        return;
    }
    chevron(cv, C - 10, 420, C, 411, C + 10, 420, col::mist);   // swipe up: stops
    cv.text(F(900, 12), "STOPS", C, 426, col::mist, Align::Center);
}

int listTop(const Model& m) { return anyStale(m) ? 114 : 92; }

void drawTabs(Canvas& cv, Tab tab) {
    const int pw = 240, ph = 46, x0 = C - pw / 2, y0 = 30;
    PlateStyle ps = smallPlate();
    ps.edge = 4;
    cv.plate(x0, y0, pw, ph, ps);
    const uint8_t steps[2] = {2, 1};
    cv.fillPixelShape(tab == Tab::Trains ? x0 + 5 : C + 1, y0 + 5, pw / 2 - 6, ph - 10, 3, steps, 2, col::headlight);
    cv.text(F(900, 17), "Trains", C - pw / 4.f, y0 + ph / 2.f + 1, tab == Tab::Trains ? col::navy : col::mist, Align::Center, Base::Middle);
    cv.text(F(900, 17), "Buses", C + pw / 4.f, y0 + ph / 2.f + 1, tab == Tab::Buses ? col::navy : col::mist, Align::Center, Base::Middle);
}

// Rendered cards, reused while a card's content and width stay the same (re-rendering every card every frame is
// too slow on the ESP32). A card renders into scratch (internal RAM on the device, where per-pixel work is fast)
// and is copied into the cache. Without cache memory, each card renders into scratch every time.
struct CacheEntry { char key[128]; int half; uint32_t used; };
uint16_t* gCacheMem = nullptr;
int gCacheN = 0;
CacheEntry gCache[16];
uint32_t gCacheClock = 0;

// While the list moves, any cached width of the same card will do (anyHalf); it settles to the exact one.
const uint16_t* cardImage(const Card& c, Tab tab, int half, bool anyHalf, const CardView& v, uint32_t ms, uint16_t* scratch) {
    char key[128];
    snprintf(key, sizeof(key), "%d|%u|%s|%s|%d|%s|%d%d%d|%s|%s%s%s", static_cast<int>(tab), c.station, c.rec.route,
             c.rec.dir, v.min, v.sched ? v.clock : "", v.delayed, v.alert, v.stale, c.heading,
             c.nPips > 0 ? c.pips[0] : "", c.nPips > 1 ? c.pips[1] : "", c.nPips > 2 ? c.pips[2] : "");
    int slot = -1;
    if (gCacheN) {
        int lru = 0;
        for (int e = 0; e < gCacheN; ++e) {
            if (strcmp(gCache[e].key, key) == 0 && (anyHalf || gCache[e].half == half)) { gCache[e].used = ++gCacheClock; return gCacheMem + e * kScratchPixels; }
            if (gCache[e].used < gCache[lru].used) lru = e;
        }
        slot = lru;
    }
    for (int p = 0; p < kScratchPixels; ++p) scratch[p] = kKey;
    Canvas off(scratch, 340, CARD_H + 6);
    routeCard(off, 170 - half, 0, half * 2, CARD_H, v, ms, false);
    if (slot >= 0) {
        memcpy(gCacheMem + slot * kScratchPixels, scratch, kScratchPixels * sizeof(uint16_t));
        snprintf(gCache[slot].key, sizeof(gCache[slot].key), "%s", key);
        gCache[slot].half = half;
        gCache[slot].used = ++gCacheClock;
    }
    return scratch;   // blitted right away, before scratch is reused
}

void drawStops(Canvas& cv, const App& a, const Model& m, uint32_t ms, Tab tab, uint16_t* scratch) {
    drawTabs(cv, tab);
    if (anyStale(m)) {
        char s[40];
        staleText(m, s, sizeof(s));
        cv.text(F(800, 14), s, C, 88, col::amber, Align::Center);
    }
    const bool trains = tab == Tab::Trains;
    const Card* items = trains ? m.trains : m.buses;
    const int n = trains ? m.nTrains : m.nBuses;
    if (n == 0 && !anyStale(m)) {   // offline, the line above says why
        const char* msg = !m.loaded ? "Loading\xE2\x80\xA6" : trains ? "No trains due nearby" : "No buses due nearby";
        cv.text(F(900, 20), msg, C, FOCUS - 12, col::mist, Align::Center);
    }
    const float scroll = a.scroll[static_cast<int>(tab)];
    const bool moving = a.drag.down || fabsf(a.scroll[static_cast<int>(tab)] - a.scrollTarget[static_cast<int>(tab)]) > 0.5f || a.trans.on;
    const float savedAlpha = cv.alpha;
    cv.clip(0, listTop(m), W, W);
    for (int i = 0; i < n; ++i) {
        const float cy = FOCUS + i * ROW - scroll;
        if (cy < -60 || cy > 540) continue;
        const float k = fminf(fabsf(cy - FOCUS) / ROW, 3);
        const float scale = 1 - 0.08f * fminf(k, 2), al = fmaxf(0.2f, 1 - 0.4f * fminf(k, 1) - 0.2f * fmaxf(0, k - 1));
        if (cv.hidden(C - 170 * scale, cy - CARD_H / 2.f * scale, 340 * scale, (CARD_H + 6) * scale)) continue;   // not in this band
        const float chord = fminf(chordHalf(cy - CARD_H / 2.f * scale), chordHalf(cy + (CARD_H / 2.f + 5) * scale));
        const int half = 4 * static_cast<int>(lroundf(fmaxf(130, fminf(170, (chord - 12) / scale)) / 4));
        const CardView v = viewOf(items[i], !trains, m);
        const uint16_t* img;
        img = cardImage(items[i], tab, half, moving, v, ms, scratch);
        const float bcy = cy + 3 * scale;
        // The list's background is black and cards never overlap, so fading a card needn't read under it.
        if (moving) cv.blitNearest(img, 340, CARD_H + 6, kKey, C, bcy, scale, al, true);
        else cv.blitScaled(img, 340, CARD_H + 6, kKey, C, bcy, scale, al, true);
        if (!v.sched && !v.stale) {   // the blinking live mark, drawn over the cached card
            const Theme& t = th(v.route, v.bus);
            const float gx = (170 + half - 14 - 16) - 170.f, gy = 16 - (CARD_H + 6) / 2.f;
            liveGlyph(cv, static_cast<int>(lroundf(C + gx * scale)), static_cast<int>(lroundf(bcy + gy * scale)), t.text, true, ms);
        }
    }
    cv.resetClip();
    cv.alpha = savedAlpha;
    const float maxS = fmaxf(1, (n - 1) * ROW), a0 = -0.42f, a1 = 0.42f;
    cv.arc(C, C, 222, a0, a1, 4, col::white, 0.12f, true);
    const float span = (a1 - a0) * 0.22f, pos = a0 + (a1 - a0 - span) * fmaxf(0, fminf(1, scroll / maxS));
    cv.arc(C, C, 222, pos, pos + span, 6, col::headlight, 1, true);
}

const Card* findCard(const Model& m, const View& v, bool& bus) {
    bus = v.tab == Tab::Buses;
    const Card* cs = bus ? m.buses : m.trains;
    const int n = bus ? m.nBuses : m.nTrains;
    for (int i = 0; i < n; ++i)
        if (cs[i].station == v.station && strcmp(cs[i].rec.route, v.route) == 0 && strcmp(cs[i].rec.dir, v.dir) == 0) return &cs[i];
    return nullptr;   // gone (its last arrival passed, or the line stopped showing up): tick() goes back
}

void drawDetail(Canvas& cv, App& a, const Model& m, uint32_t ms, const View& v) {
    bool bus;
    const Card* hero = findCard(m, v, bus);
    if (!hero) return;
    const Theme& t = th(hero->rec.route, bus);
    const bool stale = cta::isStale(hero->rec, m.now);
    const int s = static_cast<int>(lroundf(a.detailScroll));
    const int savedOy = cv.oy;
    cv.oy -= s;
    // the line's huge name with a hard pixel drop shadow in its dark shade
    const char* name = bus ? hero->rec.route : mock::lineName(hero->rec.route);
    static const int kTitle[] = {84, 76, 68, 60, 52, 44};
    const Font* ft = F(900, 44);
    for (int size : kTitle) { ft = F(900, size); if (Canvas::measure(ft, name) <= 2 * chordHalf(58 - s) - 70) break; }
    cv.text(ft, name, C + 5, 24 + 5, t.card, Align::Center);
    cv.text(ft, name, C, 24, t.text, Align::Center);
    cv.text(F(800, 17), hero->rec.name, C, 118, col::mist, Align::Center);
    char buf[48];
    const Font* fh = F(900, 21);
    const float hw = fminf(260, Canvas::measure(fh, hero->heading)), hx = C - (hw + 28) / 2;
    cv.arrowCircle(hx + 10, 154, 10, t.text, col::navy);
    cv.text(fh, Canvas::fit(fh, hero->heading, 260, buf, sizeof(buf)), hx + 28, 142, t.text);
    // departure tiles: the next one outlined in the light shade
    const int tw = 100, th2 = 112, gap = 10, x0 = C - (3 * tw + 2 * gap) / 2, ty = 182;
    for (int i = 0; i < 3 && i < hero->rec.count; ++i) {
        const int mins = hero->rec.arrivals[i].min;
        const int x = x0 + i * (tw + gap);
        const bool lead = i == 0;
        const float cx = x + tw / 2.f;
        const bool sched = !hero->rec.arrivals[i].live, live = !sched;   // per tile: CTA mixes the two
        const float saved = cv.alpha;
        if (stale) cv.alpha *= 0.6f;
        PlateStyle ps;
        ps.fill = t.top; ps.fill2 = t.card; ps.rim = t.text; ps.rimAlpha = lead ? 1.f : 0.25f; ps.rimW = lead ? 3 : 2;
        cv.plate(x, ty, tw, th2, ps);
        char big[8];
        if (sched) mock::clockText(m.now, mins, big, sizeof(big));
        else snprintf(big, sizeof(big), mins <= 1 ? "Due" : "%d", mins);
        const int sc = sched ? 3 : mins <= 1 ? 4 : lead ? 6 : 5;
        const int dw = Canvas::pixelWidth(big, sc), gw = live && !stale ? 20 : 0;
        const int gx = static_cast<int>(lroundf(cx - (dw + gw) / 2.f));
        cv.pixelText(big, gx, ty + 16 + (lead || sched ? 0 : 4), sc, t.text);
        if (gw) liveGlyph(cv, gx + dw + 4, ty + 16, t.text, true, ms);
        if (!sched && mins > 1) cv.text(F(800, 12), "minutes", cx, ty + 64, t.text, Align::Center);
        if (hero->rec.arrivals[i].delayed || (i > 0 && hero->delayMin)) {
            const float w = Canvas::measure(F(900, 11), "DELAYED") + 12;
            cv.chip("DELAYED", static_cast<int>(cx - w / 2), ty + th2 - 28, col::amber, 0x000000);
        } else if (sched) {
            const float w = Canvas::measure(F(900, 11), "SCHEDULED") + 12;
            cv.chip("SCHEDULED", static_cast<int>(cx - w / 2), ty + th2 - 28, t.text, t.card);
        }
        cv.alpha = saved;
    }
    // freshness chip, then the alert row
    int y = 316;
    char fresh[40];
    if (stale) snprintf(fresh, sizeof(fresh), "Updated %d min ago", static_cast<int>((m.now - hero->rec.ts) / 60));
    else snprintf(fresh, sizeof(fresh), "Live \xC2\xB7 updated %d s ago", static_cast<int>(m.now - hero->rec.ts));
    const float fw = Canvas::measure(F(800, 13), fresh) + (stale ? 24 : 46);
    PlateStyle sp = smallPlate();
    sp.edge = 4;
    cv.plate(static_cast<int>(C - fw / 2), y, static_cast<int>(fw), 32, sp);
    if (!stale) liveGlyph(cv, static_cast<int>(C - fw / 2 + 12), y + 8, col::signal, false, ms);
    cv.text(F(800, 13), fresh, C - fw / 2 + (stale ? 12 : 34), y + 17, stale ? col::amber : col::white, Align::Left, Base::Middle);
    y += 48;
    if (hero->rec.alert) {
        cv.plate(C - 150, y, 300, 40, sp);
        cv.ink(kAlert, 16, C - 136, y + 12, 1, col::white);
        char msg[48];
        if (bus) snprintf(msg, sizeof(msg), "Service alert on the %s", hero->rec.route);
        else snprintf(msg, sizeof(msg), "Service alert on the %s Line", mock::lineName(hero->rec.route));
        cv.text(F(800, 14), Canvas::fit(F(800, 14), msg, 240, buf, sizeof(buf)), C - 112, y + 21, col::white, Align::Left, Base::Middle);
        y += 56;
    }
    // the stop's other lines and directions, as cards in their colors
    if (!bus) {
        bool header = false;
        for (int i = 0; i < m.nTrains; ++i) {
            const Card& g = m.trains[i];
            if (g.station != hero->station || &g == hero) continue;
            if (!header) { cv.text(F(900, 12), "AT THIS STOP", C, y, col::mist, Align::Center); y += 24; header = true; }
            const Theme& gt = th(g.rec.route, false);
            const float top = y - s;
            const int half = static_cast<int>(fmaxf(120, fminf(160, fminf(chordHalf(top), chordHalf(top + 77)) - 16)));
            const int x = C - half, w = half * 2;
            const float saved = cv.alpha;
            if (stale) cv.alpha *= 0.6f;
            PlateStyle ps;
            ps.fill = gt.top; ps.fill2 = gt.card; ps.rim = gt.text; ps.rimAlpha = 0.25f;
            cv.plate(x, y, w, 72, ps);
            const int mins = g.rec.arrivals[0].min;
            char big[8];
            if (g.sched) mock::clockText(m.now, mins, big, sizeof(big));
            else snprintf(big, sizeof(big), mins <= 1 ? "Due" : "%d", mins);
            const int R = x + w - 16;
            const int dw = cv.pixelText(big, R, y + (g.sched ? 22 : 14), g.sched ? 3 : 4, gt.text, Align::Right);
            if (!g.sched && mins > 1) cv.text(F(800, 12), "min", R - dw / 2.f, y + 46, gt.text, Align::Center);
            cv.text(F(900, 24), mock::lineName(g.rec.route), x + 16, y + 6, gt.text);
            cv.arrowCircle(x + 23, y + 52, 7, gt.text, gt.card);
            cv.text(F(900, 14), Canvas::fit(F(900, 14), g.heading, w - dw - 72, buf, sizeof(buf)), x + 36, y + 43, gt.text);
            if (g.rec.arrivals[0].delayed) cv.chip("Delayed", x + w - dw - 96, y + 10, col::amber, 0x000000);
            cv.alpha = saved;
            y += 86;
        }
    }
    a.detailH = y;
    cv.oy = savedOy;
    chevron(cv, 30, C - 12, 20, C, 30, C + 12, col::mist);   // back
}

void drawPair(Canvas& cv, uint32_t ms) {
    const bool up = (ms / 500) % 2 == 0;
    drawPet(cv, cta::Mood::Happy, false, C - 48, 52 + (up ? -3 : 0), 3, nullptr, true);
    cv.pixelText("482 913", C, 196, 7, col::white, Align::Center);
    cv.text(F(800, 18), "Type this code on your iPhone", C, 266, col::mist, Align::Center);
    cv.text(F(900, 15), "Trackie-3F2A", C, 298, col::headlight, Align::Center);
}

void drawMapSoon(Canvas& cv, const Model& m, uint32_t ms) {
    const bool up = (ms / 500) % 2 == 0;
    const mock::WeatherView wx = mock::weatherView(m);
    drawPet(cv, m.mood, m.docked, C - 64, 120 + (up ? -4 : 0), 4, &wx, true);
    cv.text(F(900, 20), "The map comes next", C, 300, col::white, Align::Center);
    cv.text(F(800, 15), "Swipe right for Home", C, 330, col::mist, Align::Center);
    chevron(cv, 30, C - 12, 20, C, 30, C + 12, col::mist);
}

bool isAod(const App& a, const View& v) { return a.dim && (v.screen == Screen::Home || v.screen == Screen::First); }

void drawView(Canvas& cv, App& a, const Model& m, const View& v, uint32_t ms, uint16_t* scratch) {
    const bool aod = isAod(a, v);
    switch (v.screen) {
        case Screen::Home:
        case Screen::First: drawHome(cv, a, m, ms, aod, v); break;
        case Screen::Stops: drawStops(cv, a, m, ms, v.tab, scratch); break;
        case Screen::Detail: drawDetail(cv, a, m, ms, v); break;
        case Screen::Pair: drawPair(cv, ms); break;
        case Screen::Map: drawMapSoon(cv, m, ms); break;
    }
}

// ---------- navigation ----------
void go(App& a, const View& v, int axis, int dir, uint32_t ms) {   // axis: 0 none, 1 x, 2 y
    if (axis) { a.trans.on = true; a.trans.from = a.view; a.trans.xAxis = axis == 1; a.trans.dir = dir; a.trans.t0 = ms; }
    else a.trans.on = false;
    a.view = v;
}
View with(View v, Screen s) { v.screen = s; return v; }
View withTab(View v, Tab t) { v.tab = t; return v; }
void back(App& a, uint32_t ms) { View v = a.view; v.screen = v.back; v.back = Screen::Stops; go(a, v, 1, -1, ms); }
int listCount(const Model& m, Tab t) { return t == Tab::Trains ? m.nTrains : m.nBuses; }
float maxScroll(const Model& m, Tab t) { return fmaxf(0, (listCount(m, t) - 1) * ROW); }
void snap(App& a, const Model& m, Tab t, float v) {
    a.scrollTarget[static_cast<int>(t)] = fmaxf(0, fminf(maxScroll(m, t), roundf(v / ROW) * ROW));
}
float detailMax(const App& a) { return fmaxf(0, a.detailH - 430); }
void openDetail(App& a, const Card& c, Tab tab, Screen backTo, uint32_t ms) {
    View v = a.view;
    v.screen = Screen::Detail; v.tab = tab; v.station = c.station; v.back = backTo;
    snprintf(v.route, sizeof(v.route), "%s", c.rec.route);
    snprintf(v.dir, sizeof(v.dir), "%s", c.rec.dir);
    a.detailScroll = a.detailTarget = 0;
    go(a, v, 1, 1, ms);
}
// Gesture feel. Velocities are px/ms.
constexpr float kSnapTau = 51;      // ms; a list settling after a slow drag (the old 0.28-per-frame easing)
constexpr float kFlingTau = 250;    // ms; a flung list leaves at the finger's speed and travels speed × this
constexpr float kFlingMin = 0.3f;   // slower than this, a release just settles
constexpr float kFlickV = 0.5f;     // a short drag this fast still counts as a swipe
constexpr float kMaxV = 4.f;

void addSample(App& a, float x, float y, uint32_t ms) {
    auto& d = a.drag;
    d.hist[d.nHist % 8] = {x, y, ms};
    ++d.nHist;
}

// The finger's velocity over its last 80 ms, or zero if it had stopped before lifting.
void releaseVelocity(const App& a, float& vx, float& vy) {
    const auto& d = a.drag;
    vx = vy = 0;
    if (d.nHist < 2) return;
    const auto& last = d.hist[(d.nHist - 1) % 8];
    const auto* old = &last;
    const uint32_t n = d.nHist < 8 ? d.nHist : 8;
    for (uint32_t k = 2; k <= n; ++k) {
        const auto& s = d.hist[(d.nHist - k) % 8];
        if (last.t - s.t > 80) break;
        old = &s;
    }
    const float dt = static_cast<float>(last.t - old->t);
    if (dt < 12) return;
    vx = fmaxf(-kMaxV, fminf(kMaxV, (last.x - old->x) / dt));
    vy = fmaxf(-kMaxV, fminf(kMaxV, (last.y - old->y) / dt));
}

void toast(App& a, const char* msg, uint32_t ms) { snprintf(a.toast, sizeof(a.toast), "%s", msg); a.toastT0 = ms; }
void wake(App& a, uint32_t ms) { a.dim = false; a.lastInput = ms; }

void tap(App& a, Model& m, float x, float y, uint32_t ms) {
    const Screen sc = a.view.screen;
    if (sc == Screen::Home) {
        if (inRect(x, y, gPetTile)) { a.poked = true; a.pokeT0 = ms; }
        else if (x > 410 && fabsf(y - C) < 60) go(a, with(a.view, Screen::Map), 1, 1, ms);
        else if (inRect(x, y, gHomeCard) || y > 400) go(a, with(a.view, Screen::Stops), 2, 1, ms);
    } else if (sc == Screen::Stops) {
        if (y < 86) {
            const Tab t = x < C ? Tab::Trains : Tab::Buses;
            if (t != a.view.tab) go(a, withTab(a.view, t), 1, t == Tab::Buses ? 1 : -1, ms);
            return;
        }
        const Tab t = a.view.tab;
        const int i = static_cast<int>(lroundf((y - FOCUS + a.scroll[static_cast<int>(t)]) / ROW));
        if (i >= 0 && i < listCount(m, t)) {
            snap(a, m, t, i * ROW);
            openDetail(a, t == Tab::Trains ? m.trains[i] : m.buses[i], t, Screen::Stops, ms);
        }
    } else if (sc == Screen::Detail && x < 80) back(a, ms);
    else if (sc == Screen::Map && x < 80) go(a, with(a.view, Screen::Home), 1, -1, ms);
}

}  // namespace

void setCardCache(uint16_t* mem, int entries) {
    gCacheMem = mem;
    gCacheN = mem ? (entries > 16 ? 16 : entries) : 0;
    for (CacheEntry& e : gCache) { e.key[0] = 0; e.used = 0; }
}

void init(App& a, uint32_t ms) { a = App(); a.lastInput = ms; a.lastTick = ms; }

float brightness(const App& a, const Model& m) {
    if (a.dim) return m.docked ? 0.16f : 0.35f;
    return m.docked ? 0.45f : 1.f;
}

bool animating(const App& a, const Model& m, uint32_t ms) {
    if (a.trans.on || a.drag.down) return true;
    for (int t = 0; t < 2; ++t) if (fabsf(a.scroll[t] - a.scrollTarget[t]) > 0.5f) return true;
    if (fabsf(a.detailScroll - a.detailTarget) > 0.5f) return true;
    if (a.poked && ms - a.pokeT0 < 400) return true;
    if (a.toast[0] && ms - a.toastT0 < 1900) return true;
    return a.view.screen == Screen::Home && !m.docked && m.weather != mock::Weather::Clear;
}

bool alwaysOn(const App& a) { return isAod(a, a.view) && !a.trans.on; }

void cycleScenario(App& a, Model& m, uint32_t ms) {
    m.scenario = static_cast<Scenario>((static_cast<int>(m.scenario) + 1) % static_cast<int>(Scenario::Count));
    View v = a.view;
    v.screen = m.scenario == Scenario::FirstRun ? Screen::First : m.scenario == Scenario::Pairing ? Screen::Pair : Screen::Home;
    a.trans.on = false;
    a.view = v;
    char msg[40];
    snprintf(msg, sizeof(msg), "Debug: %s", mock::scenarioName(m.scenario));
    toast(a, msg, ms);
}

void cycleWeather(App& a, Model& m, uint32_t ms) {
    if (m.live && m.weatherForced && m.weather == mock::Weather::Snow) {   // live: the cycle ends back on real weather
        m.weatherForced = false;
        m.weather = m.hasWeather ? mock::weatherFor(m.wmo) : mock::Weather::Clear;
        toast(a, "Weather: live", ms);
        return;
    }
    m.weather = static_cast<mock::Weather>((static_cast<int>(m.weather) + 1) % static_cast<int>(mock::Weather::Count));
    m.weatherForced = true;   // live weather stops overriding it until the cycle comes back around
    static const char* const kNames[] = {"Weather: clear", "Weather: rain", "Weather: snow"};
    toast(a, kNames[static_cast<int>(m.weather)], ms);
}

void tick(App& a, Model& m, uint32_t ms) {
    const float dt = static_cast<float>(ms - a.lastTick);
    a.lastTick = ms;
    const uint32_t idle = ms - a.lastInput;
    if (!a.dim && idle > cta::kScreenDimSec * 1000 && !a.drag.down) a.dim = true;
    if (a.dim && idle > 45000 && a.view.screen != Screen::Home && a.view.screen != Screen::Pair && a.view.screen != Screen::First) {
        a.view.screen = Screen::Home; a.trans.on = false;
    }
    if (!a.drag.down) {
        auto ease = [&](float& v, float target, float tau) {
            v += (target - v) * (1 - expf(-dt / tau));
            if (fabsf(target - v) < 0.5f) v = target;
        };
        for (int t = 0; t < 2; ++t) ease(a.scroll[t], a.scrollTarget[t], a.scrollTau[t]);
        ease(a.detailScroll, a.detailTarget, a.detailTau);
    }
    if (a.poked && ms - a.pokeT0 > 2600) a.poked = false;
    if (a.trans.on && ms - a.trans.t0 >= 220) a.trans.on = false;
    // The open detail's card is gone: back to the list, rather than showing some other stop.
    bool bus;
    if (a.view.screen == Screen::Detail && !a.trans.on && !findCard(m, a.view, bus)) {
        back(a, ms);
        toast(a, "No more arrivals there", ms);
    }
    // the debug gesture: hold Trackie
    if (a.drag.down && !a.drag.moved && !a.drag.isLong && !a.drag.wasDim && ms - a.drag.t0 > 650) {
        if (a.view.screen == Screen::Home && inRect(a.drag.x0, a.drag.y0, gPetTile)) { a.drag.isLong = true; cycleScenario(a, m, ms); }
        else if ((a.view.screen == Screen::Home && a.drag.y0 < 100) || a.view.screen == Screen::Pair || a.view.screen == Screen::First) {
            a.drag.isLong = true;
            if (a.view.screen == Screen::Home) cycleWeather(a, m, ms); else cycleScenario(a, m, ms);
        }
    }
}

void draw(Canvas& cv, App& a, const Model& m, uint32_t ms, uint16_t* scratch) {
    cv.clear(col::bg);
    cv.ox = cv.oy = 0;
    cv.alpha = 1;
    if (a.dim) {   // shift a few pixels every 10 s so nothing burns in
        const uint32_t k = ms / 10000;
        cv.ox = static_cast<int>((k * 3) % 7) - 3;
        cv.oy = static_cast<int>((k * 5) % 7) - 3;
    }
    const int bx = cv.ox, by = cv.oy;
    if (a.trans.on) {
        const float t = fminf(1, (ms - a.trans.t0) / 220.f), p = 1 - powf(1 - t, 3), D = W * a.trans.dir;
        const int fx = a.trans.xAxis ? static_cast<int>(-p * D) : 0, fy = a.trans.xAxis ? 0 : static_cast<int>(-p * D);
        const int tx = a.trans.xAxis ? static_cast<int>((1 - p) * D) : 0, ty = a.trans.xAxis ? 0 : static_cast<int>((1 - p) * D);
        cv.ox = bx + fx; cv.oy = by + fy;
        drawView(cv, a, m, a.trans.from, ms, scratch);
        cv.ox = bx + tx; cv.oy = by + ty;
        drawView(cv, a, m, a.view, ms, scratch);
    } else drawView(cv, a, m, a.view, ms, scratch);
    cv.ox = cv.oy = 0;
    if (a.toast[0] && ms - a.toastT0 < 1800 && !a.dim) {
        const float w = Canvas::measure(F(900, 13), a.toast) + 24;
        PlateStyle ps = smallPlate();
        ps.fill = ps.fill2 = col::headlight; ps.rim = col::navy; ps.rimAlpha = 1; ps.edge = 3;
        cv.plate(static_cast<int>(C - w / 2), 436 - 34, static_cast<int>(w), 26, ps);
        cv.text(F(900, 13), a.toast, C, 436 - 34 + 13, col::navy, Align::Center, Base::Middle);
    }
}

void pointerDown(App& a, Model& m, float x, float y, uint32_t ms) {
    (void)m;
    auto& d = a.drag;
    const int t = static_cast<int>(a.view.tab);
    const Screen sc = a.view.screen;
    // A press on a moving list stops it where it is, and lifting that press doesn't count as a tap.
    d.caught = (sc == Screen::Stops && fabsf(a.scroll[t] - a.scrollTarget[t]) > 3) ||
               (sc == Screen::Detail && fabsf(a.detailScroll - a.detailTarget) > 3);
    d.down = true; d.moved = false; d.isLong = false; d.wasDim = a.dim;
    d.x0 = d.x = x; d.y0 = d.y = y; d.t0 = ms;
    d.s0 = sc == Screen::Stops ? a.scroll[t] : a.detailScroll;
    d.mx0 = a.mapX; d.my0 = a.mapY;
    d.nHist = 0;
    addSample(a, x, y, ms);
}

void pointerMove(App& a, Model& m, float x, float y, uint32_t ms) {
    (void)m;
    auto& d = a.drag;
    if (!d.down) return;
    d.x = x; d.y = y;
    addSample(a, x, y, ms);
    const float dx = x - d.x0, dy = y - d.y0;
    if (hypotf(dx, dy) > 9) d.moved = true;
    if (d.wasDim || !d.moved || d.isLong || fabsf(dy) < fabsf(dx)) return;
    if (a.view.screen == Screen::Stops) {
        const int t = static_cast<int>(a.view.tab);
        float v = d.s0 - dy;
        if (v < 0) v *= 0.4f;
        a.scroll[t] = a.scrollTarget[t] = v;
    }
    if (a.view.screen == Screen::Detail) {
        a.detailScroll = a.detailTarget = fmaxf(-40, fminf(detailMax(a) + 40, d.s0 - dy));
    }
}

// Writes what a release did into a.gesture when it goes out of scope (the serial gesture log).
struct UpLog {
    App& a;
    Screen on;   // where the release happened (the gesture may move to another screen)
    float dx = 0, dy = 0, vx = 0, vy = 0;
    uint32_t held = 0, samples = 0;
    char what[48] = "nothing";
    explicit UpLog(App& app) : a(app), on(app.view.screen) {}
    ~UpLog() {
        static const char* const kScreens[] = {"home", "stops", "detail", "map", "pair", "first"};
        snprintf(a.gesture, sizeof(a.gesture), "up on %s: moved %+.0f,%+.0f in %u ms (%u samples), v %+.2f,%+.2f px/ms -> %s",
                 kScreens[static_cast<int>(on)], dx, dy, static_cast<unsigned>(held),
                 static_cast<unsigned>(samples), vx, vy, what);
    }
};

void pointerUp(App& a, Model& m, float x, float y, uint32_t ms) {
    if (!a.drag.down) return;
    UpLog log(a);
    // The lift itself repeats the last point; adding it again would only dilute the speed.
    const auto& last = a.drag.hist[(a.drag.nHist - 1) % 8];
    if (a.drag.nHist == 0 || last.x != x || last.y != y) addSample(a, x, y, ms);
    float vx, vy;
    releaseVelocity(a, vx, vy);
    const auto d = a.drag;
    a.drag.down = false;
    const float dx = x - d.x0, dy = y - d.y0;
    log.dx = dx; log.dy = dy; log.vx = vx; log.vy = vy; log.held = ms - d.t0; log.samples = d.nHist;
    if (d.wasDim) { wake(a, ms); snprintf(log.what, sizeof(log.what), "wake"); return; }
    a.lastInput = ms;
    if (d.isLong) { snprintf(log.what, sizeof(log.what), "long press"); return; }
    const Screen sc = a.view.screen;
    const int t = static_cast<int>(a.view.tab);
    const bool horizontal = fabsf(dx) > fabsf(dy);
    // Lists settle where they are, or carry on at the finger's speed and slow to a stop on a card.
    auto settle = [&](bool fling) {
        if (sc == Screen::Stops) {
            const bool f = fling && fabsf(vy) > kFlingMin;
            a.scrollTau[t] = f ? kFlingTau : kSnapTau;
            snap(a, m, a.view.tab, a.scroll[t] - (f ? vy * kFlingTau : 0));
            snprintf(log.what, sizeof(log.what), "%s from %.2f cards to card %d", f ? "fling" : "settle",
                     a.scroll[t] / ROW, static_cast<int>(lroundf(a.scrollTarget[t] / ROW)));
        } else if (sc == Screen::Detail) {
            const bool f = fling && fabsf(vy) > kFlingMin;
            a.detailTau = f ? kFlingTau : kSnapTau;
            a.detailTarget = fmaxf(0, fminf(detailMax(a), a.detailScroll - (f ? vy * kFlingTau : 0)));
            snprintf(log.what, sizeof(log.what), "%s from %.0f to %.0f px", f ? "fling" : "settle", a.detailScroll,
                     a.detailTarget);
        }
    };
    if (!d.moved) {
        if (d.caught) settle(false);
        else { tap(a, m, d.x0, d.y0, ms); snprintf(log.what, sizeof(log.what), "tap"); }
        return;
    }
    // A swipe is a long drag or a short quick flick.
    const bool swipeX = horizontal && (fabsf(dx) > 40 || (fabsf(dx) > 16 && fabsf(vx) > kFlickV));
    const bool flickUp = !horizontal && (dy < -40 || (dy < -16 && vy < -kFlickV));
    const bool flickDown = !horizontal && (dy > 40 || (dy > 16 && vy > kFlickV));
    if (swipeX) {
        snprintf(log.what, sizeof(log.what), "swipe %s", dx < 0 ? "left" : "right");
        if (sc == Screen::Stops) {
            if (dx < 0 && a.view.tab == Tab::Trains) go(a, withTab(a.view, Tab::Buses), 1, 1, ms);
            else if (dx > 0 && a.view.tab == Tab::Buses) go(a, withTab(a.view, Tab::Trains), 1, -1, ms);
            else settle(false);
        } else if (sc == Screen::Detail && dx > 0) back(a, ms);
        else if (sc == Screen::Home && dx < 0) go(a, with(a.view, Screen::Map), 1, 1, ms);
        else if (sc == Screen::Map && dx > 0) go(a, with(a.view, Screen::Home), 1, -1, ms);
        else settle(false);
        return;
    }
    if (sc == Screen::Home && flickUp) { go(a, with(a.view, Screen::Stops), 2, 1, ms); snprintf(log.what, sizeof(log.what), "to stops"); return; }
    if (sc == Screen::Stops && (a.scroll[t] < -30 || (d.y0 < 90 && flickDown))) {
        snprintf(log.what, sizeof(log.what), "to home");
        a.scroll[t] = a.scrollTarget[t] = 0;
        go(a, with(a.view, Screen::Home), 2, -1, ms);
        return;
    }
    settle(!horizontal);
}

}  // namespace ui
