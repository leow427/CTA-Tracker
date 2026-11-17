#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "alerts_weather.h"
#include "card_theme.h"
#include "cta_config.h"
#include "device_hooks.h"
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
constexpr uint16_t kKey = 0x0001;         // transparent in the card scratch buffer

const Font* F(int w, int s) { return Canvas::font(w, s); }

float chordHalf(float y) { const float d = fabsf(y - C); return d >= C ? 0 : sqrtf(C * C - d * d); }

// ---------- Trackie, via the shared pet renderer ----------
// wx: the weather for his umbrella or scarf; nullptr for none.
void drawPet(Canvas& cv, cta::Mood mood, bool docked, int x, int y, int s, const mock::WeatherView* wx, bool extra) {
    ui::drawPet(cv, mood, docked, x, y, s, wx ? cta::accessoryFor(wx->wmo, wx->tempC) : cta::Accessory::None, extra);
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
    char compass;
    bool star;          // the pinned stop: a ★ before the card's title
    const char* line;   // in place of where it goes: a follow's "Red · 4 min · to 95th"
    bool noTime;        // no minutes (a follow that's Arrived or Lost)
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
    v.compass = c.compass;
    return v;
}

// The direction letter beside a card's name: a small square in the card's text color with the letter cut out of
// it, like a route shield. Returns the room it takes, gap included (0 with no letter).
float compassBadge(Canvas& cv, char letter, float x, float cy, int size, const Theme& t) {
    if (!letter) return 0;
    const uint8_t steps[1] = {1};
    if (t.outline)
        cv.fillPixelShape(static_cast<int>(x) - 1, static_cast<int>(cy) - size / 2 - 1, size + 2, size + 2, 2, steps, 1, t.outline);
    cv.fillPixelShape(static_cast<int>(x), static_cast<int>(cy) - size / 2, size, size, 2, steps, 1, t.text);
    const char str[2] = {letter, 0};
    cv.text(F(900, size >= 24 ? 18 : 16), str, x + size / 2.f, cy + 0.5f, t.card, Align::Center, Base::Middle);
    return size + 8;
}

// The pin's ★ as pixel art (Manrope has no ★), in the card's text color, outlined on a bus, centred on cy.
// Returns the room it takes, gap included.
const char* const kStar[9] = {"....K....", "....K....", "...KKK...", "KKKKKKKKK", ".KKKKKKK.",
                              "..KKKKK..", "..KK.KK..", ".KK...KK.", ".K.....K."};
float star(Canvas& cv, bool on, float x, float cy, int s, const Theme& t) {
    if (!on) return 0;
    inkRing(cv, kStar, 9, static_cast<int>(x), static_cast<int>(cy) - 9 * s / 2, s, t.text, t.outline);
    return 9 * s + 8;
}

void routeCard(Canvas& cv, int x, int y, int w, int h, const CardView& c, uint32_t ms, bool glyph = true) {
    const Theme& t = th(c.route, c.bus);
    const float saved = cv.alpha;
    if (c.stale) cv.alpha *= 0.55f;
    PlateStyle ps;
    ps.fill = t.top; ps.fill2 = t.card;
    if (t.outline) { ps.rim = t.outline; ps.rimAlpha = 1; ps.rimW = 3; }
    else { ps.rim = t.text; ps.rimAlpha = 0.25f; }
    cv.plate(x, y, w, h, ps);
    char buf[48];
    const int R = x + w - 14;
    int rightW;
    if (c.sched) {   // the clock time with the still blue glyph, and SCHEDULED under it (the app's ScheduledTag)
        const int dw = Canvas::pixelWidth(c.clock, 3), dx = R - 20 - dw;
        cardDigits(cv, c.clock, dx, y + 20, 3, t);
        liveGlyph(cv, R - 16, y + 16, false, false, ms, t.halo);
        const float tw = Canvas::measure(F(900, 11), "SCHEDULED") + 12;
        const float tx = fminf(dx + dw / 2.f - tw / 2, R - tw);
        cv.chip("SCHEDULED", static_cast<int>(tx), y + 50, t.text, t.card);
        rightW = static_cast<int>(fmaxf(dw + 20, R - tx));
    } else if (c.noTime) {
        rightW = 0;
    } else {
        const bool due = c.min >= 0 && c.min <= 1;
        const int s = due ? 4 : 5;
        char big[8];
        snprintf(big, sizeof(big), c.min < 0 ? "--" : due ? "Due" : "%d", c.min);   // -1: a follow not on the card yet
        const int dw = Canvas::pixelWidth(big, s), dx = R - 20 - dw;
        cardDigits(cv, big, dx, y + (due ? 28 : 16), s, t);
        if (glyph || c.stale) liveGlyph(cv, R - 16, y + 16, true, !c.stale, ms, t.halo);
        if (!due && c.min >= 0) cardText(cv, F(800, 12), "minutes", dx + dw / 2.f, y + 58, t, Align::Center);
        rightW = dw + 20;
    }
    const int L = x + 16;
    const float maxW = w - 32 - rightW - 10;
    constexpr float kPip = 14, kPipStep = kPip + 4;   // a pip and the room it takes
    auto pips = [&](float px, float py) {   // the station's other lines, as squares in their colors
        for (int i = 0; i < c.nPips; ++i) {
            const Theme& pt = th(c.pips[i], false);
            cv.fillRect(px, py, kPip, kPip, t.text);
            cv.fillRect(px + 2, py + 2, kPip - 4, kPip - 4, pt.accent);
            px += kPipStep;
        }
    };
    if (c.bus) {   // the route number, then where it goes, then the stop
        const Font* fName = F(900, 34);
        const float badgeW = c.compass ? 24 + 8 : 0;
        const float nx = L + star(cv, c.star, L, y + 24, 3, t);
        const char* name = Canvas::fit(fName, c.route, maxW - (nx - L) - badgeW - (c.alert ? 26 : 0), buf, sizeof(buf));
        const float nw = cardText(cv, fName, name, nx, y + 4, t);
        const float bw = compassBadge(cv, c.compass, nx + nw + 10, y + 24, 24, t);
        if (c.alert) inkRing(cv, kAlert, 16, static_cast<int>(nx + nw + 8 + bw), y + 16, 1, t.text, t.outline);
        const Font* fHead = F(900, 16);
        if (c.line) cardText(cv, fHead, Canvas::fit(fHead, c.line, maxW, buf, sizeof(buf)), L, y + 49, t);
        else {
            cardArrow(cv, L + 8, y + 59, 8, t);
            // With the letter beside the number, "South to Harlem" says only where it goes: "To Harlem".
            const char* head = c.heading;
            char hb[40];
            if (c.compass)
                if (const char* to = strstr(c.heading, " to ")) { snprintf(hb, sizeof(hb), "To %s", to + 4); head = hb; }
            cardText(cv, fHead, Canvas::fit(fHead, head, maxW - 22, buf, sizeof(buf)), L + 22, y + 49, t);
        }
        int sx = L;
        if (c.delayed) sx += cv.chip("Delayed", L, y + 72, col::amber, 0x000000) + 6;
        const Font* fStop = F(800, 13);
        // A scheduled time's tag reaches down beside the stop, so the stop stops short of it.
        const float stopW = (c.sched ? maxW : maxW + rightW) - (sx - L) - c.nPips * kPipStep - 4;
        const char* stop = Canvas::fit(fStop, c.stop, stopW, buf, sizeof(buf));
        pips(sx + cardText(cv, fStop, stop, sx, y + 73, t) + 8, y + 74);
    } else {       // a train's color names its line, so the stop leads, large, then where it goes
        const float pipW = c.nPips ? c.nPips * kPipStep : 0;
        const float badgeW = c.compass ? 20 + 8 : 0;
        const float nx = L + star(cv, c.star, L, y + 29, 2, t);
        const float nameW = maxW - (nx - L) - badgeW - pipW - (c.alert ? 26 : 0);
        const Font* fName = F(900, 24);
        static const int kSmaller[] = {21, 19};   // step down before truncating
        for (int size : kSmaller)
            if (Canvas::measure(fName, c.stop) > nameW) fName = F(900, size);
        const int ny = 15 + (24 - fName->size) * 2 / 3;
        const float nw = cardText(cv, fName, Canvas::fit(fName, c.stop, nameW, buf, sizeof(buf)), nx, y + ny, t);
        const float bw = compassBadge(cv, c.compass, nx + nw + 8, y + 29, 20, t);
        pips(nx + nw + 8 + bw, y + 22);
        if (c.alert) inkRing(cv, kAlert, 16, static_cast<int>(nx + nw + 8 + bw + pipW), y + 21, 1, t.text, t.outline);
        int hx = L;
        if (c.delayed) hx += cv.chip("Delayed", L, y + 58, col::amber, 0x000000) + 6;
        const Font* fHead = F(900, 16);
        if (c.line) cardText(cv, fHead, Canvas::fit(fHead, c.line, maxW - (hx - L), buf, sizeof(buf)), hx, y + 57, t);
        else {
            cardArrow(cv, hx + 8, y + 67, 8, t);
            cardText(cv, fHead, Canvas::fit(fHead, c.heading, maxW - (hx - L) - 22, buf, sizeof(buf)), hx + 22, y + 57, t);
        }
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

struct Rect { int x, y, w, h; };
bool inRect(float x, float y, const Rect& r) { return x > r.x && x < r.x + r.w && y > r.y && y < r.y + r.h; }
Rect gPetTile{0, 0, 0, 0}, gHomeCard{0, 0, 0, 0};

// ---------- screens ----------
// The dim always-on screen's motion: Trackie bobs once a second, and the whole screen shifts a few pixels every 10 s
// so nothing burns in. aodKey() reads these too.
int aodBob(uint32_t ms) { return (ms / 1000) % 2 ? 0 : -4; }
void aodShift(uint32_t ms, int& ox, int& oy) {
    const uint32_t k = ms / 10000;
    ox = static_cast<int>((k * 3) % 7) - 3;
    oy = static_cast<int>((k * 5) % 7) - 3;
}

// A follow on the pinned card (ecosystem spec 5.7): its minutes in the big digits and "Red · 4 min · to 95th" where
// the card says where it goes, then "Due", then "Arrived" or "Lost the train" with no time until the live side
// drops it (cta::followExpired).
void followCard(CardView& v, const cta::FollowView& f, const Card& c, char* line, size_t cap) {
    const char* who = v.bus ? c.rec.route : mock::lineName(c.rec.route);
    // Where it goes, from the heading ("To 95th/Dan Ryan", "South to Harrison"): a bus's dir is "Southbound".
    const char* dest = strstr(c.heading, " to ");
    dest = dest ? dest + 4 : strncmp(c.heading, "To ", 3) == 0 ? c.heading + 3 : c.heading;
    char to[sizeof(c.heading)];
    snprintf(to, sizeof(to), "%s", dest);
    if (char* slash = strchr(to, '/')) *slash = 0;   // "95th/Dan Ryan" → "95th", as the spec writes it
    v.sched = v.delayed = false;                     // the card's next arrival's, not the followed vehicle's
    v.line = line;
    if (f.state == cta::FollowState::Arrived || f.state == cta::FollowState::Lost) {
        snprintf(line, cap, "%s", f.state == cta::FollowState::Arrived ? "Arrived" : v.bus ? "Lost the bus" : "Lost the train");
        v.noTime = true;
        return;
    }
    v.min = f.state == cta::FollowState::Due ? 0 : f.min;
    if (v.min < 0) snprintf(line, cap, "%s \xC2\xB7 to %s", who, to);
    else if (v.min <= 1) snprintf(line, cap, "%s \xC2\xB7 Due \xC2\xB7 to %s", who, to);
    else snprintf(line, cap, "%s \xC2\xB7 %d min \xC2\xB7 to %s", who, v.min, to);
}

void drawHome(Canvas& cv, const App& a, const Model& m, uint32_t ms, bool aod, const View& v) {
    const bool docked = m.docked, first = v.screen == Screen::First, stale = anyStale(m);
    drawStatusRow(cv, m, 68, aod || docked);
    const bool up = (ms / 500) % 2 == 0;
    if (aod) { drawPet(cv, m.mood, docked, C - 64, 146 + aodBob(ms), 4, nullptr, false); return; }   // see aodKey()
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
    if (const Card* card = m.hasPin ? &m.pin : m.glance) {
        CardView cvw = viewOf(*card, m.hasPin ? card->rec.type == cta::Kind::Bus : m.glanceIsBus, m);
        cvw.star = m.hasPin;
        char line[48];
        if (m.hasPin && m.hasFollow) followCard(cvw, m.follow, *card, line, sizeof(line));
        else if (m.hasPin && !card->rec.count) cvw.noTime = true, cvw.sched = false;   // a pin with nothing due yet
        gHomeCard = {C - 160, 290, 320, CARD_H};
        routeCard(cv, C - 160, 290, 320, CARD_H, cvw, ms);
    }
    if (v.screen != Screen::Home) return;
    chevron(cv, 436, C - 12, 446, C, 436, C + 12, col::mist);   // swipe left: Settings
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
// too slow on the ESP32). A card renders straight into the cache entry it replaces (the least recently used);
// without cache memory, into scratch every time. The entries' keys live in the cache memory too (PSRAM on the
// device), and a small index in internal RAM (each key's hash, width, and last use) keeps the lookup, a dozen times
// a frame while the list scrolls, from reading them: a key is only compared in full when its hash matches.
constexpr int kMaxCacheEntries = 32;
struct CacheKey { char key[128]; };
struct CacheIndex { uint32_t hash; int half; uint32_t used; };
uint16_t* gCacheMem = nullptr;
int gCacheN = 0;
CacheKey* gCache = nullptr;
CacheIndex gIndex[kMaxCacheEntries];
uint32_t gCacheClock = 0;

uint32_t keyHash(const char* s) {   // FNV-1a
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}
uint32_t gCardRenders = 0;   // cards rendered, cached or not (cardRenders())

// Everything a rendered card shows (its cache key).
void cardKey(const Card& c, Tab tab, const CardView& v, char* key, size_t cap) {
    snprintf(key, cap, "%d|%u|%s|%s|%d|%s|%d%d%d|%s|%s%s%s", static_cast<int>(tab), c.station, c.rec.route,
             c.rec.dir, v.min, v.sched ? v.clock : "", v.delayed, v.alert, v.stale, c.heading,
             c.nPips > 0 ? c.pips[0] : "", c.nPips > 1 ? c.pips[1] : "", c.nPips > 2 ? c.pips[2] : "");
}

// Where card i of a list scrolled to `scroll` sits: its centre, scale, fade, the width it's drawn at, and its live
// mark's top left (before the canvas translation). False when it's off the screen.
struct CardSlot { float cy, scale, al, bcy; int half, markX, markY; };
bool cardSlot(int i, float scroll, CardSlot& s) {
    s.cy = FOCUS + i * ROW - scroll;
    if (s.cy < -60 || s.cy > 540) return false;
    const float k = fminf(fabsf(s.cy - FOCUS) / ROW, 3);
    s.scale = 1 - 0.08f * fminf(k, 2);
    s.al = fmaxf(0.2f, 1 - 0.4f * fminf(k, 1) - 0.2f * fmaxf(0, k - 1));
    const float chord = fminf(chordHalf(s.cy - CARD_H / 2.f * s.scale), chordHalf(s.cy + (CARD_H / 2.f + 5) * s.scale));
    s.half = 4 * static_cast<int>(lroundf(fmaxf(130, fminf(170, (chord - 12) / s.scale)) / 4));
    s.bcy = s.cy + 3 * s.scale;
    const float gx = (170 + s.half - 14 - 16) - 170.f, gy = 16 - (CARD_H + 6) / 2.f;
    s.markX = static_cast<int>(lroundf(C + gx * s.scale));
    s.markY = static_cast<int>(lroundf(s.bcy + gy * s.scale));
    return true;
}

// While the list moves (or slides in a transition), any cached width of the same card will do (anyHalf), the exact
// one first; it settles to the exact one.
const uint16_t* cardImage(const Card& c, Tab tab, int half, bool anyHalf, const CardView& v, uint32_t ms, uint16_t* scratch) {
    char key[128];
    cardKey(c, tab, v, key, sizeof(key));
    int slot = -1;
    const uint32_t h = keyHash(key);
    if (gCacheN) {
        int lru = 0, other = -1;   // other: the same card at another width
        for (int e = 0; e < gCacheN; ++e) {
            if (gIndex[e].hash == h && strcmp(gCache[e].key, key) == 0) {
                if (gIndex[e].half == half) { gIndex[e].used = ++gCacheClock; return gCacheMem + e * kScratchPixels; }
                if (other < 0) other = e;
            }
            if (gIndex[e].used < gIndex[lru].used) lru = e;
        }
        if (anyHalf && other >= 0) { gIndex[other].used = ++gCacheClock; return gCacheMem + other * kScratchPixels; }
        slot = lru;
    }
    ++gCardRenders;
    uint16_t* const img = slot >= 0 ? gCacheMem + slot * kScratchPixels : scratch;
    for (int p = 0; p < kScratchPixels; ++p) img[p] = kKey;
    Canvas off(img, 340, CARD_H + 6);
    routeCard(off, 170 - half, 0, half * 2, CARD_H, v, ms, false);
    if (slot >= 0) {
        snprintf(gCache[slot].key, sizeof(gCache[slot].key), "%s", key);
        gIndex[slot] = {h, half, ++gCacheClock};
    }
    return img;   // scratch is blitted right away, before it's reused
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
        CardSlot sl;
        if (!cardSlot(i, scroll, sl)) continue;
        const float scale = sl.scale;
        if (cv.hidden(C - 170 * scale, sl.cy - CARD_H / 2.f * scale, 340 * scale, (CARD_H + 6) * scale)) continue;   // not in this band
        const CardView v = viewOf(items[i], !trains, m);
        const uint16_t* img = cardImage(items[i], tab, sl.half, moving, v, ms, scratch);
        // The list's background is black and cards never overlap, so fading a card needn't read under it.
        if (moving) cv.blitNearest(img, 340, CARD_H + 6, kKey, C, sl.bcy, scale, sl.al, true);
        else cv.blitScaled(img, 340, CARD_H + 6, kKey, C, sl.bcy, scale, sl.al, true);
        if (!v.sched && !v.stale)   // the blinking live mark, drawn over the cached card
            liveGlyph(cv, sl.markX, sl.markY, true, true, ms, th(v.route, v.bus).halo);
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
    char buf[48];
    if (bus) {
        // the route's huge number with a hard pixel drop shadow in its dark shade, then where it goes
        static const int kTitle[] = {84, 76, 68, 60, 52, 44};
        const Font* ft = F(900, 44);
        for (int size : kTitle) { ft = F(900, size); if (Canvas::measure(ft, hero->rec.route) <= 2 * chordHalf(58 - s) - 70) break; }
        // As the app's map header: the light blue shadow, the gray number outlined in light blue.
        cv.text(ft, hero->rec.route, C + 5, 24 + 5, t.outline, Align::Center);
        cardText(cv, ft, hero->rec.route, C, 24, t, Align::Center);
        // The stop and where it goes on a white bus plate, as the app backs a bus page's header (its gray needs it).
        const Font* fh = F(900, 21);
        const float hw = fminf(260, Canvas::measure(fh, hero->heading)), hx = C - (hw + 28) / 2;
        const int pw = static_cast<int>(fmaxf(hw + 28, Canvas::measure(F(800, 17), hero->rec.name)) + 32);
        PlateStyle hp;
        hp.fill = t.top; hp.fill2 = t.card; hp.rim = t.outline; hp.rimAlpha = 1; hp.rimW = 3;
        cv.plate(static_cast<int>(C - pw / 2.f), 108, pw, 66, hp);
        cardText(cv, F(800, 17), hero->rec.name, C, 116, t, Align::Center);
        cardArrow(cv, hx + 10, 152, 10, t);
        cardText(cv, fh, Canvas::fit(fh, hero->heading, 260, buf, sizeof(buf)), hx + 28, 140, t);
    } else {
        // a train's color names its line, so the stop is the title, with the same drop shadow, then where it goes
        static const int kTitle[] = {68, 60, 52, 44, 34};
        const float maxT = 2 * chordHalf(74 - s) - 60;
        const Font* ft = F(900, 34);
        for (int size : kTitle) {
            ft = F(900, size);
            if (Canvas::covers(ft, hero->rec.name) && Canvas::measure(ft, hero->rec.name) <= maxT) break;
        }
        const char* title = Canvas::fit(ft, hero->rec.name, maxT, buf, sizeof(buf));
        const int ty0 = 40 + (68 - ft->size) / 2;   // a smaller title sits centered in the 68 px one's space
        cv.text(ft, title, C + 4, ty0 + 4, t.card, Align::Center);
        cv.text(ft, title, C, ty0, t.text, Align::Center);
        const Font* fh = F(900, 21);
        const float hw = fminf(260, Canvas::measure(fh, hero->heading)), hx = C - (hw + 28) / 2;
        cv.arrowCircle(hx + 10, 144, 10, t.text, col::navy);
        cv.text(fh, Canvas::fit(fh, hero->heading, 260, buf, sizeof(buf)), hx + 28, 132, t.text);
    }
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
        ps.fill = t.top; ps.fill2 = t.card; ps.rimW = lead ? 3 : 2;
        if (t.outline) { ps.rim = t.outline; ps.rimAlpha = 1; }
        else { ps.rim = t.text; ps.rimAlpha = lead ? 1.f : 0.25f; }
        cv.plate(x, ty, tw, th2, ps);
        char big[8];
        if (sched) mock::clockText(m.now, mins, big, sizeof(big));
        else snprintf(big, sizeof(big), mins <= 1 ? "Due" : "%d", mins);
        const int sc = sched ? 3 : mins <= 1 ? 4 : lead ? 6 : 5;
        // The glyph: light green while live and fresh (still on a tile), blue when scheduled, none once stale.
        const int dw = Canvas::pixelWidth(big, sc), gw = sched || !stale ? 20 : 0;
        const int gx = static_cast<int>(lroundf(cx - (dw + gw) / 2.f));
        cardDigits(cv, big, gx, ty + 16 + (lead || sched ? 0 : 4), sc, t);
        if (gw) liveGlyph(cv, gx + dw + 4, ty + 16, live, false, ms, t.halo);
        if (!sched && mins > 1) cardText(cv, F(800, 12), "minutes", cx, ty + 64, t, Align::Center);
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
    // Both on the line's plate in its text color, as the app's chips and alert row (amber is only for Delayed).
    PlateStyle sp = smallPlate();
    sp.edge = 4;
    sp.fill = t.top; sp.fill2 = t.card;
    if (t.outline) { sp.rim = t.outline; sp.rimAlpha = 1; }
    cv.plate(static_cast<int>(C - fw / 2), y, static_cast<int>(fw), 32, sp);
    if (!stale) liveGlyph(cv, static_cast<int>(C - fw / 2 + 12), y + 8, true, false, ms, t.halo);
    cardText(cv, F(800, 13), fresh, C - fw / 2 + (stale ? 12 : 34), y + 17, t, Align::Left, Base::Middle);
    y += 48;
    if (hero->rec.alert) {
        cv.plate(C - 150, y, 300, 40, sp);
        inkRing(cv, kAlert, 16, C - 136, y + 12, 1, t.text, t.outline);
        char msg[48];
        if (bus) snprintf(msg, sizeof(msg), "Service alert on the %s", hero->rec.route);
        else snprintf(msg, sizeof(msg), "Service alert on the %s Line", mock::lineName(hero->rec.route));
        cardText(cv, F(800, 14), Canvas::fit(F(800, 14), msg, 240, buf, sizeof(buf)), C - 112, y + 21, t, Align::Left, Base::Middle);
        y += 56;
    }
    // the stop's other lines and directions, as cards in their colors
    if (!bus) {
        bool header = false;
        for (int i = 0; i < m.nTrains; ++i) {
            const Card& g = m.trains[i];
            if (g.station != hero->station || &g == hero) continue;
            if (!header) { cv.text(F(900, 12), "AT THIS STOP", C, y, t.text, Align::Center); y += 24; header = true; }
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
            int dw;   // the arrival block's width
            if (g.sched) {   // as the app's line cards: the time, the still blue glyph, SCHEDULED under them
                const int cw = Canvas::pixelWidth(big, 3);
                cv.pixelText(big, R - 20 - cw, y + 14, 3, gt.text);
                liveGlyph(cv, R - 16, y + 12, false, false, ms, 0);
                const int tw = static_cast<int>(Canvas::measure(F(900, 11), "SCHEDULED")) + 12;
                cv.chip("SCHEDULED", R - tw, y + 42, gt.text, gt.card);
                dw = cw + 20 > tw ? cw + 20 : tw;
            } else {
                dw = cv.pixelText(big, R, y + 14, 4, gt.text, Align::Right);
                if (mins > 1) cv.text(F(800, 12), "min", R - dw / 2.f, y + 46, gt.text, Align::Center);
            }
            cv.arrowCircle(x + 25, y + 36, 9, gt.text, gt.card);
            cv.text(F(900, 21), Canvas::fit(F(900, 21), g.heading, w - dw - 72, buf, sizeof(buf)), x + 41, y + 22, gt.text);
            if (g.rec.arrivals[0].delayed) cv.chip("Delayed", x + w - dw - 96, y + 10, col::amber, 0x000000);
            cv.alpha = saved;
            y += 86;
        }
    }
    a.detailH = y;
    cv.oy = savedOy;
    chevron(cv, 30, C - 12, 20, C, 30, C + 12, col::mist);   // back
}

void drawPair(Canvas& cv, const Model& m, uint32_t ms) {
    const bool up = (ms / 500) % 2 == 0;
    drawPet(cv, cta::Mood::Happy, false, C - 48, 52 + (up ? -3 : 0), 3, nullptr, true);
    const uint32_t key = m.passkey ? m.passkey : m.scenario == Scenario::Pairing ? 482913 : 0;
    if (key) {
        char code[8];
        snprintf(code, sizeof(code), "%03u %03u", static_cast<unsigned>(key / 1000 % 1000),
                 static_cast<unsigned>(key % 1000));
        cv.pixelText(code, C, 196, 7, col::white, Align::Center);
        cv.text(F(800, 18), "Type this code on your iPhone", C, 266, col::mist, Align::Center);
    } else if (m.pairCode) {   // the Wi-Fi fallback's code, typed into the app (not iOS's prompt)
        char code[8];
        snprintf(code, sizeof(code), "%03u %03u", static_cast<unsigned>(m.pairCode / 1000 % 1000),
                 static_cast<unsigned>(m.pairCode % 1000));
        cv.pixelText(code, C, 196, 7, col::white, Align::Center);
        cv.text(F(800, 18), "Type this code in the app", C, 266, col::mist, Align::Center);
    } else if (m.pairWaitS) {
        char wait[40];
        snprintf(wait, sizeof(wait), "Too many tries, new code in %u s", static_cast<unsigned>(m.pairWaitS));
        cv.text(F(800, 18), wait, C, 266, col::mist, Align::Center);
    } else {   // waiting for the phone: no code until iOS asks for one
        cv.text(F(800, 18), "Pair in the app on your iPhone", C, 266, col::mist, Align::Center);
    }
    cv.text(F(900, 15), m.pairName, C, 298, col::headlight, Align::Center);
}

// ---------- Settings: the wearable's own page ----------
// Rows of 320 × 56 inside the circle, then the Pair again button; the confirm step's two buttons. All at least 56 px
// tall, so they're easy to hit.
constexpr int kRowX = C - 160, kRowW = 320, kRowH = 56;
constexpr Rect kPairBtn{C - 120, 356, 240, 56}, kYesBtn{C - 120, 236, 240, 60}, kNoBtn{C - 120, 310, 240, 60};
// The brightness row's − and + plates (48 × 44 in the 56 px row); a tap anywhere in the row's height on either side
// hits them (kMinusHit, kPlusHit).
constexpr int kBrightY = 288, kScreenMinPct = 20;   // as the dock's floor (cta::kDimFloorPct): still readable
constexpr Rect kMinusBtn{kRowX + kRowW - 162, kBrightY + 6, 48, 44}, kPlusBtn{kRowX + kRowW - 54, kBrightY + 6, 48, 44};
constexpr Rect kMinusHit{kMinusBtn.x - 12, kBrightY - 4, 72, kRowH + 8}, kPlusHit{kPlusBtn.x - 12, kBrightY - 4, 72, kRowH + 8};

void button(Canvas& cv, const Rect& r, const char* label, bool primary) {
    PlateStyle ps;
    if (primary) { ps.fill = ps.fill2 = col::headlight; ps.rim = col::navy; ps.rimAlpha = 1; }   // the active control
    cv.plate(r.x, r.y, r.w, r.h, ps);
    cv.text(F(900, 20), label, C, r.y + r.h / 2.f + 1, primary ? col::navy : col::white, Align::Center, Base::Middle);
}

// A row: its label on the left; returns the value's vertical centre.
float settingsRow(Canvas& cv, int y, const char* label) {
    cv.plate(kRowX, y, kRowW, kRowH, PlateStyle());
    const float cy = y + kRowH / 2.f;
    cv.text(F(800, 16), label, kRowX + 18, cy, col::mist, Align::Left, Base::Middle);
    return cy;
}

void drawSettings(Canvas& cv, const Model& m) {
    char buf[48], name[32];
    if (m.petName[0]) snprintf(name, sizeof(name), "%s", m.petName);
    else snprintf(name, sizeof(name), "CTA-Pet-%s", m.deviceId);
    cv.text(F(900, 12), "SETTINGS", C, 34, col::mist, Align::Center);
    cv.text(F(900, 24), Canvas::fit(F(900, 24), name, 280, buf, sizeof(buf)), C, 54, col::white, Align::Center);
    const int R = kRowX + kRowW - 18;
    const Font* fv = F(900, 19);
    // Wi-Fi: the network and its bars, or why there's none.
    float cy = settingsRow(cv, 96, "Wi-Fi");
    if (m.ssid[0]) {
        const int bars = wifiBars(m.rssi);
        for (int i = 0; i < 4; ++i) {   // 5 px bars, 6 to 18 px tall, on the row's baseline
            const int h = 6 + 4 * i;
            cv.fillRect(R - 29 + i * 8, cy + 9 - h, 5, h, i < bars ? col::white : col::dimInk);
        }
        cv.text(fv, Canvas::fit(fv, m.ssid, kRowW - 36 - 60 - 39, buf, sizeof(buf)), R - 39, cy + 1, col::white, Align::Right, Base::Middle);
    } else {
        cv.text(fv, m.wifiSetUp ? "Not connected" : "Not set up", R, cy + 1, col::mist, Align::Right, Base::Middle);
    }
    // Battery: a pixel battery filled to the level (green while charging, amber when low), then the words.
    cy = settingsRow(cv, 160, "Battery");
    if (m.battery < 0) {
        cv.text(fv, "On USB power", R, cy + 1, col::white, Align::Right, Base::Middle);
    } else {
        const int bx = R - 34, by = static_cast<int>(cy) - 8;
        const uint32_t ink = m.charging ? col::signal : m.battery <= 20 ? col::amber : col::white;
        cv.fillRect(bx, by, 30, 2, col::white); cv.fillRect(bx, by + 14, 30, 2, col::white);
        cv.fillRect(bx, by, 2, 16, col::white); cv.fillRect(bx + 28, by, 2, 16, col::white);
        cv.fillRect(bx + 30, by + 5, 4, 6, col::white);
        cv.fillRect(bx + 4, by + 4, 22 * (m.battery > 100 ? 100 : m.battery) / 100, 8, ink);
        snprintf(buf, sizeof(buf), m.charging ? "Charging \xC2\xB7 %d%%" : "%d%%", m.battery);
        cv.text(fv, buf, R - 46, cy + 1, m.charging ? col::signal : col::white, Align::Right, Base::Middle);
    }
    cy = settingsRow(cv, 224, "Bluetooth");
    cv.text(fv, m.bleConnected ? "Connected" : "Not connected", R, cy + 1, m.bleConnected ? col::signal : col::mist,
            Align::Right, Base::Middle);
    // Brightness: − value +, each step a plate inside the row (ecosystem spec 5.2); one that can't go further is dim.
    cy = settingsRow(cv, kBrightY, "Brightness");
    snprintf(buf, sizeof(buf), "%d%%", m.screenPct);
    cv.text(fv, buf, (kMinusBtn.x + kMinusBtn.w + kPlusBtn.x) / 2.f, cy + 1, col::white, Align::Center, Base::Middle);
    for (int k = 0; k < 2; ++k) {
        const bool plus = k == 1, can = plus ? m.screenPct < 100 : m.screenPct > kScreenMinPct;
        const Rect* r = plus ? &kPlusBtn : &kMinusBtn;
        cv.plate(r->x, r->y, r->w, r->h, smallPlate());
        const int bx = r->x + r->w / 2, by = r->y + r->h / 2;
        const uint32_t ink = can ? col::white : col::dimInk;
        cv.fillRect(bx - 8, by - 1, 16, 3, ink);
        if (plus) cv.fillRect(bx - 1, by - 8, 3, 16, ink);
    }
    button(cv, kPairBtn, "Pair again", true);
    // Quiet hours (set in the app), in the chord under Pair again.
    cta::quietLine(m.nightStart, m.nightEnd, buf, sizeof(buf));
    // The circle is about 232 px wide here: drop " · set in the app" rather than cut a time.
    if (Canvas::measure(F(900, 12), buf) > 212)
        if (char* d = strstr(buf, " \xC2\xB7")) *d = 0;
    cv.text(F(900, 12), buf, C, 424, col::mist, Align::Center);
    chevron(cv, 30, C - 12, 20, C, 30, C + 12, col::mist);   // swipe right: Home
}

void drawConfirm(Canvas& cv) {
    cv.text(F(900, 34), "Pair again?", C, 112, col::white, Align::Center);
    cv.text(F(800, 18), "Your phone will need to pair again.", C, 176, col::mist, Align::Center);
    button(cv, kYesBtn, "Pair again", true);
    button(cv, kNoBtn, "Cancel", false);
}

bool isAod(const App& a, const View& v) { return a.dim && (v.screen == Screen::Home || v.screen == Screen::First); }

void drawView(Canvas& cv, App& a, const Model& m, const View& v, uint32_t ms, uint16_t* scratch) {
    const bool aod = isAod(a, v);
    switch (v.screen) {
        case Screen::Home:
        case Screen::First: drawHome(cv, a, m, ms, aod, v); break;
        case Screen::Stops: drawStops(cv, a, m, ms, v.tab, scratch); break;
        case Screen::Detail: drawDetail(cv, a, m, ms, v); break;
        case Screen::Pair: drawPair(cv, m, ms); break;
        case Screen::Settings: drawSettings(cv, m); break;
        case Screen::Confirm: drawConfirm(cv); break;
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
        else if (x > 410 && fabsf(y - C) < 60) go(a, with(a.view, Screen::Settings), 1, 1, ms);
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
    else if (sc == Screen::Settings && x < 80) go(a, with(a.view, Screen::Home), 1, -1, ms);
    else if (sc == Screen::Settings && inRect(x, y, kPairBtn)) go(a, with(a.view, Screen::Confirm), 0, 0, ms);
    else if (sc == Screen::Settings && (inRect(x, y, kMinusHit) || inRect(x, y, kPlusHit))) {
        m.screenPct = cta::stepPct(m.screenPct, inRect(x, y, kPlusHit) ? 1 : -1, kScreenMinPct);
        m.screenChanged = true;
    }
    else if (sc == Screen::Confirm && inRect(x, y, kYesBtn)) {
        device::pairAgain();   // wipes the bond and token and enters pairing mode (Track W)
        go(a, with(a.view, Screen::Pair), 0, 0, ms);
    } else if (sc == Screen::Confirm && inRect(x, y, kNoBtn)) go(a, with(a.view, Screen::Settings), 0, 0, ms);
}

}  // namespace

size_t cardCacheBytes(int entries) {
    return static_cast<size_t>(entries) * (kScratchPixels * sizeof(uint16_t) + sizeof(CacheKey));
}

void setCardCache(void* mem, int entries) {
    if (entries > kMaxCacheEntries) entries = kMaxCacheEntries;   // the rest of mem goes unused
    gCacheMem = static_cast<uint16_t*>(mem);
    gCacheN = mem ? entries : 0;
    gCache = mem ? reinterpret_cast<CacheKey*>(gCacheMem + static_cast<size_t>(entries) * kScratchPixels) : nullptr;
    for (int e = 0; e < gCacheN; ++e) {
        gCache[e].key[0] = 0;
        gIndex[e] = {keyHash(""), 0, 0};
    }
}

uint32_t cardRenders() { return gCardRenders; }

int wifiBars(int rssi) { return rssi == 0 ? 0 : rssi >= -55 ? 4 : rssi >= -66 ? 3 : rssi >= -77 ? 2 : 1; }

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

uint32_t restKey(const App& a, const Model& m, uint32_t ms, Mark* marks, int maxMarks, int& nMarks) {
    nMarks = 0;
    if (a.view.screen != Screen::Stops || a.trans.on || animating(a, m, ms)) return 0;
    if (a.toast[0] && ms - a.toastT0 < 1800 && !a.dim) return 0;   // draw()'s toast (animating() covers it too)
    uint32_t h = 2166136261u;
    auto mix = [&h](const void* p, size_t n) {
        for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t*>(p)[i]) * 16777619u;
    };
    auto mixStr = [&mix](const char* str) { mix(str, strlen(str) + 1); };
    // draw(): the burn-in shift while dim.
    int ox = 0, oy = 0;
    if (a.dim) aodShift(ms, ox, oy);
    mix(&ox, sizeof(ox));
    mix(&oy, sizeof(oy));
    // drawStops: the tabs, the stale line, the empty-list message, the cards and their marks, the scroll bar.
    const Tab tab = a.view.tab;
    const bool trains = tab == Tab::Trains, stale = anyStale(m);
    const Card* items = trains ? m.trains : m.buses;
    const int n = trains ? m.nTrains : m.nBuses;
    const float scroll = a.scroll[static_cast<int>(tab)];
    mix(&tab, sizeof(tab));
    mix(&stale, sizeof(stale));
    if (stale) {
        char t[40];
        staleText(m, t, sizeof(t));
        mixStr(t);
    }
    mix(&m.loaded, sizeof(m.loaded));
    mix(&n, sizeof(n));
    mix(&scroll, sizeof(scroll));
    for (int i = 0; i < n; ++i) {
        CardSlot sl;
        if (!cardSlot(i, scroll, sl)) continue;
        const CardView v = viewOf(items[i], !trains, m);
        char key[128];
        cardKey(items[i], tab, v, key, sizeof(key));
        mixStr(key);
        mix(&sl.half, sizeof(sl.half));
        const bool live = !v.sched && !v.stale;
        mix(&live, sizeof(live));
        if (!live) continue;
        if (nMarks == maxMarks) { nMarks = 0; return 0; }
        // The mark is 16 px; a bus's gray halo reaches one art pixel (2 px) past it all round.
        const int r = th(v.route, v.bus).halo ? 2 : 0;
        marks[nMarks++] = {static_cast<int16_t>(sl.markX + ox - r), static_cast<int16_t>(sl.markY + oy - r),
                           static_cast<int16_t>(16 + 2 * r), static_cast<int16_t>(16 + 2 * r)};
    }
    return h | 1;
}

AodKey aodKey(const App& a, const Model& m, uint32_t ms) {
    AodKey k;
    memset(&k, 0, sizeof(k));
    k.screen = static_cast<uint8_t>(a.view.screen);
    k.dim = a.dim;
    int ox, oy;
    aodShift(ms, ox, oy);
    k.ox = static_cast<int8_t>(ox);
    k.oy = static_cast<int8_t>(oy);
    k.bob = static_cast<int8_t>(aodBob(ms));
    // drawStatusRow
    k.clockKnown = m.clockKnown;
    if (m.clockKnown) mock::clockText(m.now, 0, k.clock, sizeof(k.clock));
    const mock::WeatherView wx = mock::weatherView(m);
    k.icon = wx.icon;
    k.rgb = wx.rgb;
    memcpy(k.temp, wx.temp, sizeof(k.temp));
    k.stale = anyStale(m);
    // drawPet, and brightness()
    k.mood = static_cast<uint8_t>(m.mood);
    k.docked = m.docked;
    return k;
}

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
    // Pairing again ends once the phone has paired (the debug Pairing scenario stays put); a passkey showing keeps it.
    if (a.view.screen == Screen::Pair && m.scenario != Scenario::Pairing && !m.passkey && device::paired())
        go(a, with(a.view, Screen::Home), 0, 0, ms);
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
    if (a.dim) aodShift(ms, cv.ox, cv.oy);   // shift a few pixels every 10 s so nothing burns in
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
    d.down = true; d.moved = false; d.isLong = false; d.wasDim = a.dim; d.vertical = false;
    d.x0 = d.x = x; d.y0 = d.y = y; d.t0 = ms;
    d.s0 = sc == Screen::Stops ? a.scroll[t] : a.detailScroll;
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
    // The axis is the one the press first moves along: a slow drag drifting past 45 degrees stopped the list dead.
    if (!d.moved && hypotf(dx, dy) > 9) { d.moved = true; d.vertical = fabsf(dy) >= fabsf(dx); }
    if (d.wasDim || !d.moved || d.isLong || !d.vertical) return;
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
        static const char* const kScreens[] = {"home", "stops", "detail", "settings", "pair", "first", "confirm"};
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
    const bool horizontal = !d.vertical && fabsf(dx) > fabsf(dy);   // a drag that scrolled stays a scroll
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
        else if (sc == Screen::Home && dx < 0 && m.hasPin && inRect(d.x0, d.y0, gHomeCard)) {   // started on the pin
            live::setFollow(nullptr);
            live::setPin(nullptr);
            // Gone from this frame; it stays gone only because setPin(nullptr) clears the published snapshot, which
            // the model is rebuilt from every second.
            m.hasPin = m.hasFollow = false;
            toast(a, "Unpinned", ms);
            snprintf(log.what, sizeof(log.what), "unpin");
        } else if (sc == Screen::Home && dx < 0) go(a, with(a.view, Screen::Settings), 1, 1, ms);
        else if (sc == Screen::Settings && dx > 0) go(a, with(a.view, Screen::Home), 1, -1, ms);
        else if (sc == Screen::Confirm && dx > 0) go(a, with(a.view, Screen::Settings), 1, -1, ms);   // cancel
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
