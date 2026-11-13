#include "dock_ui.h"

#include <ctype.h>
#include <stdlib.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "alerts_weather.h"
#include "card_theme.h"
#include "dock_status.h"
#include "gesture.h"
#include "cta_config.h"
#include "fonts.h"
#include "line_colors.h"
#include "local_time.h"
#include "map_frame.h"
#include "mood.h"
#include "train_map.h"

namespace dock {

using namespace ui;

namespace {

// ---------- layout (the mockup's constants, whole pixels) ----------
constexpr int STRIP_H = 80;                 // status strip: Trackie, bubble, map button, time, weather, Wi-Fi
constexpr int SEC_H = 240;                  // one stop section; section i spans y = STRIP_H + i * SEC_H
struct { int x, dy, w, h; } constexpr CARD = {12, 8, 456, 220};
constexpr int PAD = 18;                     // card text inset
struct { int w, h, dy; } constexpr TILE = {120, 100, 108};
constexpr int TILE_GAP = (CARD.w - 2 * PAD - 3 * TILE.w) / 2;   // 32
struct { int x, y, s; } constexpr PET = {16, 14, 2};
struct { int x, cy; } constexpr BUBBLE = {104, 44};
struct { int top, row, rowH; } constexpr PK = {96, 76, 64};
struct { int x, y, w, h; } constexpr MAP_BTN = {306, 18, 44, 40};
struct { int top, padX, padTop, padBottom, track, casing, maxPerLine, car; } constexpr MAP = {STRIP_H, 56, 50, 130, 6, 12, 3, 3};
struct { int x, y, w, h; } constexpr RECENTER = {W - 16 - 52, H - 20 - 48, 52, 48};
constexpr int ROUTE_ROW = 84, ROUTE_H = 70;   // the bus route list's pitch and row height
constexpr uint32_t kRed = 0xff453a;
constexpr uint32_t CASING = 0x050d1d;

const Font* F(int w, int s) { return Canvas::font(w, s); }

// The L's lines, in the picker's order, with their names.
struct LineName { const char* route; const char* name; };
constexpr LineName kLines[] = {{"Red", "Red"}, {"Blue", "Blue"}, {"Brn", "Brown"}, {"G", "Green"},
                               {"Org", "Orange"}, {"Pink", "Pink"}, {"P", "Purple"}, {"Y", "Yellow"}};
const char* lineName(const char* route) { return cta::trainLineName(route); }

// Lines sharing the North Side tracks sit side by side on the map, like a transit map.
int lineOffset(const char* route, bool bus) {
    if (bus) return 0;
    if (strcmp(route, "Brn") == 0) return 6;
    if (strcmp(route, "P") == 0) return 12;
    return 0;
}

// The local clock as "7:42" (12-hour), from the TZ environment.
void clockAt(uint32_t unix, char* out, size_t cap) {
    const time_t t = static_cast<time_t>(unix);
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(out, cap, "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
}

void upper(const char* s, char* out, size_t cap) {
    size_t n = 0;
    for (; s[n] && n + 1 < cap; ++n) out[n] = static_cast<char>(toupper(static_cast<unsigned char>(s[n])));
    out[n] = 0;
}

void hit(App& a, int x, int y, int w, int h, Act tap, Act hold = Act::None, int arg = 0) {
    if (a.nHits >= static_cast<int>(sizeof(a.hits) / sizeof(a.hits[0]))) return;
    a.hits[a.nHits++] = Hit{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h),
                            tap, hold, static_cast<int16_t>(arg)};
}

bool hasPick(const Section& s) { return s.pick.id[0] != 0; }
bool isBus(const Section& s) { return s.pick.type == cta::Kind::Bus; }

// A card is greyed out ("Wi-Fi lost", spec 7.4) when its arrivals can't be refreshed or have gone stale.
bool staleSec(const Data& d, const Section& s, const Clock& c) { return cta::cardStale(s.rec, c.now, d.wifiSetUp && !d.wifiUp); }
int ageMin(const Section& s, const Clock& c) { return c.now > s.rec.ts ? static_cast<int>((c.now - s.rec.ts) / 60) : 0; }

// ---------- pieces ----------

// The route shield: a small square in the text color with the number cut out of it.
int badge(Canvas& cv, const char* label, int x, int cy, int h, const Theme& t) {
    const Font* f = F(900, h >= 24 ? 16 : 14);
    const int w = std::max(h, static_cast<int>(ceilf(Canvas::measure(f, label))) + 10);
    const uint8_t steps[1] = {1};
    const int y = static_cast<int>(lroundf(cy - h / 2.f));
    if (t.outline) cv.fillPixelShape(x - 1, y - 1, w + 2, h + 2, 2, steps, 1, t.outline);
    cv.fillPixelShape(x, y, w, h, 2, steps, 1, t.text);
    cv.text(f, label, x + w / 2.f, cy + 0.5f, t.card, Align::Center, Base::Middle);
    return w;
}

// Pixel art in a few named colors (the map's vehicles): K outline, W white, Y headlight, C glass, A the line.
void art(Canvas& cv, const char* const* rows, int n, int x, int y, int s, uint32_t line, float a = 1.f) {
    for (int j = 0; j < n; ++j)
        for (int i = 0; rows[j][i]; ++i) {
            uint32_t rgb;
            switch (rows[j][i]) {
                case 'K': rgb = 0x0B0D14; break;
                case 'W': rgb = 0xFFFFFF; break;
                case 'Y': rgb = 0xFFD43B; break;
                case 'C': rgb = 0x9BEFF5; break;
                case 'A': rgb = line; break;
                default: continue;
            }
            cv.fillRect(x + i * s, y + j * s, s, s, rgb, a);
        }
}
const char* const kCar[7] = {".KKKKKK.", "KAAAAAAK", "KAWWWWAK", "KAWWWWAK", "KAAAAAAK", "KAYAAYAK", ".KK..KK."};   // an L car
const char* const kBusCar[7] = {"KKKKKKKK", "KWWWWWWK", "KCCCCCCK", "KCCCCCCK", "KWWWWWWK", "KWYWWYWK", ".KK..KK."};
const char* const kPin[9] = {".......KK", ".....KKKK", "...KKKKKK", ".KKKKKKK.", "KKKKKKKK.", "....KKK..", "....KKK..",
                             "....KK...", "....K...."};

cta::Accessory accessory(const Data& d) {
    return d.hasWeather ? cta::accessoryFor(d.wmo, d.tempC) : cta::Accessory::None;
}

// ---------- status strip: y 0–80 ----------
void drawStrip(Canvas& cv, App& a, const Data& d, const Clock& c, uint32_t ms) {
    const cta::Mood mood = a.debugMood >= 0 ? static_cast<cta::Mood>(a.debugMood) : moodOf(d, c);
    const bool lost = d.wifiSetUp && !d.wifiUp;
    const int bob = (ms / 500) % 2 ? 0 : -PET.s;
    drawPet(cv, mood, false, PET.x, PET.y + bob, PET.s, accessory(d), true);
    hit(a, PET.x, 0, 80, STRIP_H, Act::Poke);
    const bool poked = a.poked && ms - a.pokeMs < 2600;
    char say[48];
    if (poked) reasonText(d, c, say, sizeof(say));
    else if (lost) snprintf(say, sizeof(say), "No signal");
    else {
        static const char* const kSays[] = {"Sleepy\xE2\x80\xA6", "Uh oh\xE2\x80\xA6", "Running late", "All on time", "Go now!"};
        snprintf(say, sizeof(say), "%s", kSays[static_cast<int>(mood)]);
    }
    bubble(cv, say, BUBBLE.x, BUBBLE.cy);
    // right: the time, then weather and Wi-Fi under it
    const int R = W - 16;
    char tm[8];
    if (c.now) clockAt(c.now, tm, sizeof(tm));
    else snprintf(tm, sizeof(tm), "--:--");
    cv.pixelText(tm, R, 12, 4, col::white, Align::Right);
    const int wifiCx = R - 11;
    icon(cv, lost ? "off" : "wifi", wifiCx, 60, 3, lost ? kRed : col::dimInk);
    if (d.hasWeather) {
        const int w = d.wmo;
        const bool rain = (w >= 51 && w <= 67) || (w >= 80 && w <= 82) || w >= 95;
        const bool snow = (w >= 71 && w <= 77) || w == 85 || w == 86;
        const char* ic = snow ? "snow" : rain || w >= 2 ? "cloud" : "sun";
        const uint32_t rgb = snow ? 0xe8eef8 : rain || w >= 2 ? col::mist : col::headlight;
        char temp[8];
        snprintf(temp, sizeof(temp), "%d\xC2\xB0", static_cast<int>(lroundf(d.tempC * 9 / 5 + 32)));
        const float tw = cv.text(F(800, 18), temp, wifiCx - 22, 61, col::mist, Align::Right, Base::Middle);
        icon(cv, ic, wifiCx - 22 - tw - 14, 60, 3, rgb);
    }
    // the map button: a folded map that turns headlight yellow while the map is up (then it shows the cards icon)
    const bool onMap = a.screen == Screen::Map;
    PlateStyle ps = smallPlate();
    ps.fill = ps.fill2 = onMap ? col::headlight : col::raised;
    ps.rim = onMap ? col::navy : col::white;
    ps.rimAlpha = onMap ? 1 : 0.22f;
    ps.edge = 4;
    cv.plate(MAP_BTN.x, MAP_BTN.y, MAP_BTN.w, MAP_BTN.h, ps);
    icon(cv, onMap ? "cards" : "map", MAP_BTN.x + MAP_BTN.w / 2.f, MAP_BTN.y + MAP_BTN.h / 2.f, 3, onMap ? col::navy : col::white);
    hit(a, MAP_BTN.x - 6, 0, MAP_BTN.w + 12, STRIP_H, Act::MapToggle);
}

// ---------- one arrival tile ----------
void tile(Canvas& cv, int x, int y, const cta::StopRecord& r, int k, bool lead, const Theme& t, bool stale,
          const Clock& c, uint32_t ms) {
    const cta::Arrival& ar = r.arrivals[k];
    const float cx = x + TILE.w / 2.f;
    PlateStyle ps;
    ps.fill = t.tile; ps.fill2 = t.tile2;
    ps.rim = t.outline ? t.outline : t.text;
    ps.rimAlpha = t.outline ? 1 : lead ? 1 : 0.25f;
    ps.rimW = lead ? 3 : 2;
    ps.edge = 4;
    cv.plate(x, y, TILE.w, TILE.h, ps);
    const bool sched = !ar.live;
    const int min = cta::minutesLeft(r, k, c.now);
    char big[8];
    if (sched) clockAt(r.ts + ar.min * 60, big, sizeof(big));
    else if (min <= 1) snprintf(big, sizeof(big), "Due");
    else snprintf(big, sizeof(big), "%d", min);
    const int sc = sched ? 3 : min <= 1 ? 4 : lead ? 5 : 4;
    const int minW = sched || min <= 1 ? 0 : static_cast<int>(ceilf(Canvas::measure(F(800, 13), "min"))) + 4;
    const int gw = sched || !stale ? 20 : 0;
    const int dw = Canvas::pixelWidth(big, sc), gx = static_cast<int>(lroundf(cx - (dw + minW + gw) / 2.f));
    const int dy = y + 14 + (sc == 5 ? 0 : sc == 4 ? 4 : 8);
    cardDigits(cv, big, gx, dy, sc, t);
    if (minW) cardText(cv, F(800, 13), "min", gx + dw + 4, dy + 7 * sc, t, Align::Left, Base::Alphabetic);
    if (gw) liveGlyph(cv, gx + dw + minW + 4, y + 14, !sched, !stale, ms, t.halo);
    char buf[48];
    cardText(cv, F(800, 13), Canvas::fit(F(800, 13), ar.dest, TILE.w - 16, buf, sizeof(buf)), cx, y + 56, t, Align::Center);
    if (ar.delayed) {
        const int w = static_cast<int>(ceilf(Canvas::measure(F(900, 11), "DELAYED"))) + 12;
        cv.chip("DELAYED", static_cast<int>(lroundf(cx - w / 2.f)), y + TILE.h - 26, col::amber, 0x000000);
    } else if (sched) {
        const int w = static_cast<int>(ceilf(Canvas::measure(F(900, 11), "SCHED"))) + 12;
        cv.chip("SCHED", static_cast<int>(lroundf(cx - w / 2.f)), y + TILE.h - 26, t.text, t.card);
    }
}

void drawEmpty(Canvas& cv, App& a, int i, int x, int y, int w, int h) {
    hit(a, x, y, w, h, Act::OpenPicker, Act::OpenPicker, i);
    PlateStyle ps;
    ps.fill = col::navy; ps.fill2 = 0x060d1d; ps.rim = col::mist; ps.rimAlpha = 0.18f; ps.rimW = 2; ps.highlight = false;
    cv.plate(x, y, w, h, ps);
    const float cy = y + h / 2.f - 14;
    cv.fillCircle(x + w / 2.f, cy - 22, 22, col::raised);
    icon(cv, "plus", x + w / 2.f, cy - 22, 4, col::headlight);
    cv.text(F(900, 21), "Tap to pick a stop", x + w / 2.f, cy + 16, col::white, Align::Center);
    char row[32];
    snprintf(row, sizeof(row), "Shows on LED row %d", i + 1);
    cv.text(F(800, 14), row, x + w / 2.f, cy + 46, col::mist, Align::Center);
}

// Where the stop's trains or buses go: the train's direction, or the next bus's destination.
void heading(const Section& s, char* out, size_t cap) {
    if (isBus(s) && s.rec.count && s.rec.arrivals[0].dest[0]) snprintf(out, cap, "To %s", s.rec.arrivals[0].dest);
    else if (isBus(s)) snprintf(out, cap, "%s", s.pick.dir);
    else snprintf(out, cap, "To %s", s.pick.dir);
}

// ---------- one stop section: y = STRIP_H + i * SEC_H ----------
void drawSection(Canvas& cv, App& a, const Data& d, const Clock& c, int i, uint32_t ms) {
    const Section& s = d.sec[i];
    const int top = STRIP_H + i * SEC_H, x = CARD.x, y = top + CARD.dy, w = CARD.w, h = CARD.h;
    if (!hasPick(s)) return drawEmpty(cv, a, i, x, y, w, h);
    hit(a, x, y, w, h, Act::None, Act::OpenPicker, i);
    const Theme& t = th(s.pick.route, isBus(s));
    const bool stale = staleSec(d, s, c);
    const float saved = cv.alpha;
    if (stale) cv.alpha *= 0.55f;
    PlateStyle ps;
    ps.fill = t.top; ps.fill2 = t.card;
    ps.rim = t.outline ? t.outline : t.text;
    ps.rimAlpha = t.outline ? 1 : 0.25f;
    ps.rimW = t.outline ? 3 : 2;
    cv.plate(x, y, w, h, ps);
    const int L = x + PAD, R = x + w - PAD;
    char buf[64], up[32];
    // row 1: the line or route, and how fresh the data is
    if (isBus(s)) {
        const int lx = L + badge(cv, s.pick.route, L, y + 24, 24, t) + 8;
        upper(s.pick.routeName, up, sizeof(up));
        cardText(cv, F(900, 16), up, lx, y + 24, t, Align::Left, Base::Middle);
    } else {
        upper(lineName(s.pick.route), up, sizeof(up));
        snprintf(buf, sizeof(buf), "%s LINE", up);
        cardText(cv, F(900, 16), buf, L, y + 24, t, Align::Left, Base::Middle);
    }
    if (stale) {
        snprintf(buf, sizeof(buf), "Updated %d min ago", ageMin(s, c));
        cardText(cv, F(800, 13), buf, R, y + 24, t, Align::Right, Base::Middle);
    }
    // row 2: the stop, large, with the alert sign
    const Font* fName = F(900, 34);
    for (int sz : {24, 21})
        if (Canvas::measure(fName, s.pick.name) > w - 2 * PAD - 30) fName = F(900, sz);
    const float nw = cardText(cv, fName, Canvas::fit(fName, s.pick.name, w - 2 * PAD - 30, buf, sizeof(buf)), L,
                              y + 38 + (34 - fName->size) / 2.f, t);
    if (s.rec.alert) inkRing(cv, kAlert, 16, static_cast<int>(lroundf(L + nw + 10)), y + 50, 1, t.text, t.outline);
    // row 3: where it goes
    cardArrow(cv, L + 9, y + 89, 9, t);
    char head[48];
    heading(s, head, sizeof(head));
    cardText(cv, F(900, 20), Canvas::fit(F(900, 20), head, w - 2 * PAD - 28, buf, sizeof(buf)), L + 26, y + 78, t);
    // row 4: the next three arrivals as tiles
    if (s.rec.ts == 0 || s.rec.count == 0) {
        const char* msg = s.rec.ts == 0 ? (d.status[0] ? d.status : "Waiting for arrivals\xE2\x80\xA6") : "No arrivals right now";
        cardText(cv, F(800, 16), msg, x + w / 2.f, y + TILE.dy + TILE.h / 2.f, t, Align::Center, Base::Middle);
    } else {
        for (int k = 0; k < 3 && k < s.rec.count; ++k)
            tile(cv, L + k * (TILE.w + TILE_GAP), y + TILE.dy, s.rec, k, k == 0, t, stale, c, ms);
    }
    cv.alpha = saved;
}

void drawHome(Canvas& cv, App& a, const Data& d, const Clock& c, uint32_t ms) {
    drawStrip(cv, a, d, c, ms);
    for (int i = 0; i < cta::kDockStops; ++i) drawSection(cv, a, d, c, i, ms);
}

// ---------- stop picker (spec 7.2): Train or Bus → line or route → direction → stop ----------
void pickerHeader(Canvas& cv, App& a, const char* title, const char* sub) {
    chevron(cv, 30, 30, 20, 42, 30, 54, col::mist);
    hit(a, 0, 0, 70, 84, Act::Back);
    cv.text(F(900, 24), title, 48, 22, col::white);
    char buf[64];
    cv.text(F(800, 14), Canvas::fit(F(800, 14), sub, W - 48 - 110, buf, sizeof(buf)), 48, 54, col::mist);
    const uint8_t steps[1] = {1};
    for (int k = 0; k < 4; ++k)
        cv.fillPixelShape(W - 24 - (4 - k) * 18, 38, 12, 12, 2, steps, 1, k < a.picker.step ? col::headlight : col::raised);
}

PlateStyle cardPlate(const Theme& t, bool edge4 = false) {
    PlateStyle ps;
    ps.fill = t.top; ps.fill2 = t.card;
    ps.rim = t.outline ? t.outline : t.text;
    ps.rimAlpha = t.outline ? 1 : 0.25f;
    ps.rimW = t.outline ? 3 : 2;
    if (edge4) ps.edge = 4;
    return ps;
}

// A message in place of a list that's loading or failed (tap to try again).
void listMessage(Canvas& cv, App& a, Load st, const char* what) {
    char buf[48];
    if (st == Load::Failed) {
        snprintf(buf, sizeof(buf), "Couldn\xE2\x80\x99t load %s", what);
        cv.text(F(900, 21), buf, W / 2.f, 330, col::white, Align::Center);
        cv.text(F(800, 16), "Tap to try again", W / 2.f, 366, col::headlight, Align::Center);
        hit(a, 24, 300, W - 48, 100, Act::Retry);
    } else {
        snprintf(buf, sizeof(buf), "Loading %s\xE2\x80\xA6", what);
        cv.text(F(900, 21), buf, W / 2.f, 330, col::mist, Align::Center);
    }
}

// The fade at the bottom of a scrolling list says it goes on.
void listFade(Canvas& cv) {
    for (int k = 0; k < 12; ++k) cv.fillRect(0, H - 48 + k * 4, W, 4, col::bg, (k + 1) / 12.f);
}

// The bus lists as they stand for the picker's current route and direction: a list fetched for an earlier choice
// reads as still loading.
Load dirsState(const App& a, const PickerLists& pl) {
    return cta::pickerListFor(pl.dirsRoute, "", a.picker.line, "") || pl.dirsState == Load::Loading ? pl.dirsState : Load::Loading;
}
Load stopsState(const App& a, const PickerLists& pl) {
    return cta::pickerListFor(pl.stopsRoute, pl.stopsDir, a.picker.line, a.picker.dirName) || pl.stopsState == Load::Loading
               ? pl.stopsState : Load::Loading;
}

int trainDirs(const char* route, const char** out) { return static_cast<int>(cta::trainDirections(route, out, 4)); }

void drawPicker(Canvas& cv, App& a, const PickerLists& pl, uint32_t ms) {
    auto& p = a.picker;
    const bool bus = p.bus;
    const Theme& t = bus ? th("22", true) : th(p.line, false);
    char sub[80], buf[64];
    if (p.step == 1) {
        snprintf(sub, sizeof(sub), "Section %d \xC2\xB7 what kind?", p.section + 1);
        pickerHeader(cv, a, "Pick a stop", sub);
        const Theme& tr = th("Red", false);
        const Theme& bt = th("22", true);
        PlateStyle ps = cardPlate(tr);
        ps.rimW = 2;
        cv.plate(24, 120, 432, 250, ps);
        drawPet(cv, cta::Mood::Happy, false, 300, 170, 4, cta::Accessory::None, false);
        cv.text(F(900, 34), "Train", 48, 150, tr.text);
        cv.text(F(800, 16), "8 \xE2\x80\x98L\xE2\x80\x99 lines", 48, 196, tr.text);
        hit(a, 24, 120, 432, 250, Act::Kind, Act::None, 0);
        cv.plate(24, 400, 432, 250, cardPlate(bt));
        cardText(cv, F(900, 34), "Bus", 48, 430, bt);
        cardText(cv, F(800, 16), "Routes come live from Bus Tracker", 48, 476, bt);
        hit(a, 24, 400, 432, 250, Act::Kind, Act::None, 1);
        return;
    }
    if (p.step == 2 && !bus) {
        snprintf(sub, sizeof(sub), "Section %d \xC2\xB7 Train", p.section + 1);
        pickerHeader(cv, a, "Which line?", sub);
        for (int k = 0; k < 8; ++k) {
            const Theme& lt = th(kLines[k].route, false);
            const int x = 24 + (k % 2) * 224, y = PK.top + 16 + (k / 2) * 140;
            PlateStyle ps = cardPlate(lt);
            const bool on = strcmp(kLines[k].route, p.line) == 0;
            ps.rimAlpha = on ? 1 : 0.25f;
            ps.rimW = on ? 3 : 2;
            cv.plate(x, y, 208, 124, ps);
            cv.text(F(900, 24), kLines[k].name, x + 18, y + 18, lt.text);
            const char* dirs[4];
            const int n = trainDirs(kLines[k].route, dirs);
            for (int j = 0; j < 2 && j < n; ++j) {
                snprintf(sub, sizeof(sub), "To %s", dirs[j == 0 ? 0 : n - 1]);
                cv.text(F(800, 14), Canvas::fit(F(800, 14), sub, 176, buf, sizeof(buf)), x + 18, y + 60 + 22 * j, lt.text);
            }
            hit(a, x, y, 208, 124, Act::Line, Act::None, k);
        }
        return;
    }
    if (p.step == 2 && bus) {
        snprintf(sub, sizeof(sub), "Section %d \xC2\xB7 Bus", p.section + 1);
        pickerHeader(cv, a, "Which route?", sub);
        if (pl.routesState != Load::Ready) return listMessage(cv, a, pl.routesState, "routes");
        const int top = PK.top + 8;
        cv.clip(0, top, W, H - top);
        for (int k = 0; k < pl.nRoutes; ++k) {
            const int y = top + 4 + k * ROUTE_ROW - static_cast<int>(lroundf(p.scroll));
            if (y > H || y + ROUTE_H < top) continue;
            cv.plate(24, y, 432, ROUTE_H, cardPlate(t));
            const int bw = badge(cv, pl.routes[k].rt, 44, y + ROUTE_H / 2, 30, t);
            cardText(cv, F(900, 24), Canvas::fit(F(900, 24), pl.routes[k].name, 432 - 40 - bw - 14, buf, sizeof(buf)),
                     44 + bw + 14, y + ROUTE_H / 2.f, t, Align::Left, Base::Middle);
            hit(a, 24, std::max(y, top), 432, ROUTE_H, Act::Route, Act::None, k);
        }
        cv.resetClip();
        listFade(cv);
        return;
    }
    if (p.step == 3) {
        if (bus) snprintf(sub, sizeof(sub), "Section %d \xC2\xB7 %s %s", p.section + 1, p.line, p.lineName);
        else snprintf(sub, sizeof(sub), "Section %d \xC2\xB7 %s Line", p.section + 1, lineName(p.line));
        pickerHeader(cv, a, "Which way?", sub);
        const char* dirs[4];
        int n;
        if (bus) {
            if (dirsState(a, pl) != Load::Ready) return listMessage(cv, a, dirsState(a, pl), "directions");
            n = pl.nDirs;
            for (int k = 0; k < n; ++k) dirs[k] = pl.dirs[k].name;
        } else {
            n = trainDirs(p.line, dirs);
        }
        // Two directions as the mockup's tall cards; Green's and Purple's extra branches share the height.
        const int pitch = n <= 2 ? 230 : (H - 132 - 20) / n, ph = pitch - 30;
        for (int k = 0; k < n; ++k) {
            const int y = 132 + k * pitch;
            cv.plate(24, y, 432, ph, cardPlate(t));
            const Font* big = F(900, n <= 2 ? 34 : 24);
            char a1[48], b1[64];
            if (bus) { snprintf(a1, sizeof(a1), "%s", dirs[k]); snprintf(b1, sizeof(b1), "%s %s", p.line, p.lineName); }
            else { snprintf(a1, sizeof(a1), "To %s", dirs[k]); snprintf(b1, sizeof(b1), "Toward the %s end", dirs[k]); }
            const int cy = y + ph / 2;
            cardArrow(cv, 60, cy, n <= 2 ? 18 : 14, t);
            cardText(cv, big, Canvas::fit(big, a1, 340, buf, sizeof(buf)), 92, cy - (n <= 2 ? 20 : 18), t);
            cardText(cv, F(800, 16), Canvas::fit(F(800, 16), b1, 340, sub, sizeof(sub)), 92, cy + (n <= 2 ? 30 : 12), t);
            hit(a, 24, y, 432, ph, Act::Dir, Act::None, k);
        }
        return;
    }
    // step 4: a scrolling stop list
    if (bus) snprintf(sub, sizeof(sub), "%s %s \xC2\xB7 %s", p.line, p.lineName, p.dirName);
    else snprintf(sub, sizeof(sub), "%s Line \xC2\xB7 To %s", lineName(p.line), p.dirName);
    pickerHeader(cv, a, "Which stop?", sub);
    if (bus && stopsState(a, pl) != Load::Ready) return listMessage(cv, a, stopsState(a, pl), "stops");
    cta::StationRef refs[64];
    const int n = bus ? pl.nStops : static_cast<int>(cta::trainStations(p.line, p.dirName, refs, 64));
    const int top = PK.top + 8;
    cv.clip(0, top, W, H - top);
    for (int k = 0; k < n; ++k) {
        const int y = top + 8 + k * PK.row - static_cast<int>(lroundf(p.scroll));
        if (y > H || y + PK.rowH < top) continue;
        cv.plate(24, y, 432, PK.rowH, cardPlate(t, true));
        const char* name = bus ? pl.stops[k].name : refs[k].line->names[refs[k].index];
        cardText(cv, F(900, 21), Canvas::fit(F(900, 21), name, 392, buf, sizeof(buf)), 44, y + PK.rowH / 2.f, t,
                 Align::Left, Base::Middle);
        hit(a, 24, std::max(y, top), 432, PK.rowH, Act::Stop, Act::None, k);
    }
    cv.resetClip();
    listFade(cv);
    (void)ms;
}

float pickerMaxScroll(const App& a, const PickerLists& pl) {
    if (a.picker.step == 2 && a.picker.bus) return fmaxf(0, pl.nRoutes * ROUTE_ROW + 24 - (H - PK.top - 8));
    if (a.picker.step != 4) return 0;
    int n;
    if (a.picker.bus) n = pl.nStops;
    else { cta::StationRef refs[64]; n = static_cast<int>(cta::trainStations(a.picker.line, a.picker.dirName, refs, 64)); }
    return fmaxf(0, n * PK.row + 24 - (H - PK.top - 8));
}

// ---------- map mode: the picked stops' approach zones, their lines, and up to 3 live vehicles per line ----------
// A line's color as drawn big on black (the app's rule), worked out once per line.
uint32_t onBlack(const char* route) {
    static uint32_t rgb[8];
    static bool known[8] = {};
    for (int k = 0; k < 8; ++k)
        if (strcmp(kLines[k].route, route) == 0) {
            if (!known[k]) { rgb[k] = lifted(cta::lineColor(route), 0x000000, 3, 0); known[k] = true; }
            return rgb[k];
        }
    return lifted(cta::lineColor(route), 0x000000, 3, 0);
}

struct Pin { int i, x, y, ax, ay; };
struct Box { int x, y, w, h; };
bool overlaps(const Box& r, const Box* placed, int n) {
    for (int k = 0; k < n; ++k) {
        const Box& q = placed[k];
        if (r.x < q.x + q.w && q.x < r.x + r.w && r.y < q.y + q.h && q.y < r.y + r.h) return true;
    }
    return false;
}

void drawMap(Canvas& cv, App& a, const Data& d, const Clock& c, uint32_t ms) {
    drawStrip(cv, a, d, c, ms);
    const int px0 = a.panX, py0 = a.panY;
    // The frame: every picked zone between the pads, at one scale for both axes.
    cta::LatLon pts[cta::kDockStops * 7];
    size_t np = 0;
    bool picked[8] = {}, busPicked = false;
    for (const Section& s : d.sec) {
        if (!hasPick(s)) continue;
        for (const cta::LatLon& z : s.zone)
            if (z.lat != 0 || z.lon != 0) pts[np++] = z;
        if (isBus(s)) busPicked = true;
        else for (int k = 0; k < 8; ++k) if (strcmp(kLines[k].route, s.pick.route) == 0) picked[k] = true;
    }
    const cta::MapFrame fr = cta::mapFrame(pts, np, W, MAP.top, H, MAP.padX, MAP.padTop, MAP.padBottom);
    auto proj = [&](float lat, float lon, int& x, int& y) { cta::mapProject(fr, lat, lon, px0, py0, x, y); };
    const bool lost = d.wifiSetUp && !d.wifiUp;
    cv.clip(0, MAP.top, W, H - MAP.top);
    // The track: casings under everything, then the lines; lines nobody picked are dimmed. Every point is projected
    // once per frame (the device draws a frame band by band, calling this once per band).
    struct Cache {
        bool valid = false;
        float key[6] = {};
        int16_t train[1024][2];
        int16_t bus[cta::kDockStops][kMaxPath][2];
    };
    static Cache cache;
    float pathSum = 0;
    for (int i = 0; i < cta::kDockStops; ++i)
        for (int k = 0; k < d.busPathN[i]; ++k) pathSum += d.busPath[i][k].lat * (k + 1) + d.busPath[i][k].lon;
    const float key[6] = {fr.lat0, fr.lon0, fr.pxPerKm, static_cast<float>(px0), static_cast<float>(py0), pathSum};
    if (!cache.valid || memcmp(key, cache.key, sizeof(key)) != 0) {
        size_t n = 0;
        for (size_t k = 0; k < cta::kTrainShapeCount; ++k)
            for (int j = 0; j < cta::kTrainShapes[k].count && n < 1024; ++j, ++n) {
                int x, y;
                proj(cta::kTrainShapes[k].pts[j][0] / 1e5f, cta::kTrainShapes[k].pts[j][1] / 1e5f, x, y);
                cache.train[n][0] = static_cast<int16_t>(std::max(-30000, std::min(30000, x)));
                cache.train[n][1] = static_cast<int16_t>(std::max(-30000, std::min(30000, y)));
            }
        for (int i = 0; i < cta::kDockStops; ++i)
            for (int k = 0; k < d.busPathN[i]; ++k) {
                int x, y;
                proj(d.busPath[i][k].lat, d.busPath[i][k].lon, x, y);
                cache.bus[i][k][0] = static_cast<int16_t>(std::max(-30000, std::min(30000, x)));
                cache.bus[i][k][1] = static_cast<int16_t>(std::max(-30000, std::min(30000, y)));
            }
        memcpy(cache.key, key, sizeof(key));
        cache.valid = true;
    }
    // A segment only reaches seg() when its box meets this band (an integer test; seg's own rounding is costly on
    // the device, and most of the L's segments miss a given band).
    auto segIn = [&](int x0, int y0, int x1, int y1, int b, uint32_t rgb, float al) {
        const int lx = std::min(x0, x1) - b, ly = std::min(y0, y1) - b;
        if (cv.hidden(lx, ly, std::abs(x1 - x0) + 2 * b, std::abs(y1 - y0) + 2 * b)) return;
        cv.seg(x0, y0, x1, y1, b, rgb, al);
    };
    size_t firstPt[64];
    for (size_t k = 0, n = 0; k < cta::kTrainShapeCount && k < 64; n += cta::kTrainShapes[k].count, ++k) firstPt[k] = n;
    auto track = [&](size_t shape, int b, uint32_t rgb, float al) {
        const cta::TrainShape& sh = cta::kTrainShapes[shape];
        const int off = lineOffset(sh.route, false);
        const int16_t(*p)[2] = cache.train + firstPt[shape];
        if (firstPt[shape] + sh.count > 1024) return;
        for (int k = 1; k < sh.count; ++k) segIn(p[k - 1][0] + off, p[k - 1][1], p[k][0] + off, p[k][1], b, rgb, al);
    };
    auto pathLine = [&](int i, int b, uint32_t rgb) {
        for (int k = 1; k < d.busPathN[i]; ++k)
            segIn(cache.bus[i][k - 1][0], cache.bus[i][k - 1][1], cache.bus[i][k][0], cache.bus[i][k][1], b, rgb, 1);
    };
    auto isPicked = [&](const char* route) {
        for (int k = 0; k < 8; ++k) if (picked[k] && strcmp(kLines[k].route, route) == 0) return true;
        return false;
    };
    for (size_t k = 0; k < cta::kTrainShapeCount; ++k) track(k, MAP.casing, CASING, 1);
    for (int i = 0; i < cta::kDockStops; ++i) if (hasPick(d.sec[i]) && isBus(d.sec[i])) pathLine(i, 10, CASING);
    for (int pass = 0; pass < 2; ++pass)   // dim lines first, so the picked ones sit on top
        for (size_t k = 0; k < cta::kTrainShapeCount; ++k) {
            const cta::TrainShape& sh = cta::kTrainShapes[k];
            if (isPicked(sh.route) == (pass == 1)) track(k, MAP.track, onBlack(sh.route), pass ? 1 : 0.3f);
        }
    for (int i = 0; i < cta::kDockStops; ++i) if (hasPick(d.sec[i]) && isBus(d.sec[i])) pathLine(i, 4, kBusOutline);
    (void)busPicked;
    // Each zone's stops: small stations along the approach.
    for (const Section& s : d.sec) {
        if (!hasPick(s)) continue;
        const int off = lineOffset(s.pick.route, isBus(s));
        for (int k = 0; k < 6; ++k) {
            if (s.zone[k].lat == 0 && s.zone[k].lon == 0) continue;
            int x, y;
            proj(s.zone[k].lat, s.zone[k].lon, x, y);
            cv.dot(x + off, y, isBus(s) ? 9 : 11, CASING);
            cv.dot(x + off, y, isBus(s) ? 5 : 7, col::white);
        }
    }
    // Live vehicles, at most 3 per line, faded when the data is old: each between the stops of its lit LED.
    for (const Section& s : d.sec) {
        if (!hasPick(s)) continue;
        const bool fresh = s.posTs && c.now >= s.posTs && c.now - s.posTs <= cta::kLedStaleSec;
        int ks[3];
        const int n = cta::vehiclesFor(fresh ? s.mask : 0, dueNow(s, c.now), ks, MAP.maxPerLine);
        const int off = lineOffset(s.pick.route, isBus(s));
        const uint32_t lineRgb = isBus(s) ? kBusOutline : onBlack(s.pick.route);
        for (int j = 0; j < n; ++j) {
            cta::LatLon v;
            if (!cta::vehicleAt(s.zone, ks[j], v)) continue;
            int x, y;
            proj(v.lat, v.lon, x, y);
            art(cv, isBus(s) ? kBusCar : kCar, 7, x + off - 4 * MAP.car, y - static_cast<int>(floorf(3.5f * MAP.car)), MAP.car,
                lineRgb, lost ? 0.4f : 1.f);
        }
    }
    // The picked stops: numbered like their section and LED row, with the stop and its next arrival.
    Pin pins[cta::kDockStops];
    int nPins = 0;
    for (int i = 0; i < cta::kDockStops; ++i) {
        const Section& s = d.sec[i];
        if (!hasPick(s) || (s.zone[6].lat == 0 && s.zone[6].lon == 0)) continue;
        int x, y;
        proj(s.zone[6].lat, s.zone[6].lon, x, y);
        const int ox = x + lineOffset(s.pick.route, isBus(s));
        pins[nPins++] = Pin{i, ox, y, ox, y};
    }
    // Pins closer than a pin apart spread sideways, with a short leader back to the real spot.
    for (int it = 0; it < 4; ++it)
        for (int p = 0; p < nPins; ++p)
            for (int q = 0; q < nPins; ++q) {
                if (p == q || abs(pins[p].y - pins[q].y) >= 34) continue;
                const int dx = pins[q].x - pins[p].x, need = 40 - abs(dx);
                if (need > 0) {
                    const int sgn = dx >= 0 ? 1 : -1, half = (need + 1) / 2;
                    pins[p].x -= sgn * half;
                    pins[q].x += sgn * half;
                }
            }
    for (int p = 0; p < nPins; ++p) {
        const Pin& pn = pins[p];
        if (pn.x == pn.ax) continue;
        const Theme& t = th(d.sec[pn.i].pick.route, isBus(d.sec[pn.i]));
        cv.seg(pn.ax, pn.ay, pn.x, pn.y, 4, CASING);
        cv.seg(pn.ax, pn.ay, pn.x, pn.y, 2, t.text);
        cv.dot(pn.ax, pn.ay, 7, t.text);
    }
    // Labels: beside the pin (right, then left), else a row above or below, kept on screen and off every other pin
    // and label.
    constexpr int PIN_S = 30;
    Box placed[cta::kDockStops * 2];
    int nPlaced = 0;
    for (int p = 0; p < nPins; ++p) placed[nPlaced++] = Box{pins[p].x - PIN_S / 2 - 2, pins[p].y - PIN_S / 2 - 2, PIN_S + 4, PIN_S + 4};
    for (int p = 0; p < nPins; ++p) {
        const Pin& pn = pins[p];
        const Section& s = d.sec[pn.i];
        const Theme& t = th(s.pick.route, isBus(s));
        const int px = pn.x - PIN_S / 2, py = pn.y - PIN_S / 2;
        char when[16] = "";
        if (s.rec.count) {
            const cta::Arrival& a0 = s.rec.arrivals[0];
            const int m = cta::minutesLeft(s.rec, 0, c.now);
            if (!a0.live) clockAt(s.rec.ts + a0.min * 60, when, sizeof(when));
            else if (m <= 1) snprintf(when, sizeof(when), "Due");
            else snprintf(when, sizeof(when), "%d min", m);
        }
        const Font* fN = F(900, 15);
        const Font* fW = F(800, 14);
        const int nameW = static_cast<int>(ceilf(Canvas::measure(fN, s.pick.name)));
        const int whenW = static_cast<int>(ceilf(Canvas::measure(fW, when)));
        const int lw = nameW + whenW + (when[0] ? 30 : 20), lh = 32;
        const int cands[8][2] = {{px + PIN_S + 8, 0}, {px - 8 - lw, 0}, {px + PIN_S + 8, -40}, {px - 8 - lw, -40},
                                 {px + PIN_S + 8, 40}, {px - 8 - lw, 40}, {pn.x - lw / 2, -44}, {pn.x - lw / 2, 44}};
        int lx = 0, ly = 0;
        for (const auto& cd : cands) {
            const Box r{cd[0], pn.y - lh / 2 + cd[1], lw, lh};
            if (r.x < 8 || r.x + lw > W - 8 || r.y < MAP.top + 6 || r.y + lh > H - 6 || overlaps(r, placed, nPlaced)) continue;
            lx = r.x; ly = r.y;
            break;
        }
        if (!lx) { lx = std::max(8, std::min(W - 8 - lw, px + PIN_S + 8)); ly = pn.y - lh / 2 - 40; }
        placed[nPlaced++] = Box{lx, ly, lw, lh};
        const float saved = cv.alpha;
        if (staleSec(d, s, c)) cv.alpha *= 0.55f;
        PlateStyle ps = smallPlate();
        ps.fill = t.top; ps.fill2 = t.card;
        ps.rim = t.outline ? t.outline : t.text;
        ps.rimAlpha = t.outline ? 1 : 0.3f;
        ps.rimW = 2; ps.edge = 3;
        cv.plate(lx, ly, lw, lh, ps);
        cardText(cv, fN, s.pick.name, lx + 10, ly + lh / 2.f + 1, t, Align::Left, Base::Middle);
        if (when[0]) cardText(cv, fW, when, lx + 20 + nameW, ly + lh / 2.f + 1, t, Align::Left, Base::Middle);
        PlateStyle pp = smallPlate();
        pp.fill = pp.fill2 = t.text; pp.rim = CASING; pp.rimAlpha = 1; pp.rimW = 2; pp.edge = 3;
        cv.plate(px, py, PIN_S, PIN_S, pp);
        char num[4];
        snprintf(num, sizeof(num), "%d", pn.i + 1);
        cv.pixelText(num, pn.x + 1, py + 8, 2, t.outline ? 0xffffff : t.card, Align::Center);
        cv.alpha = saved;
        hit(a, px, py, PIN_S, PIN_S, Act::Home);
        hit(a, lx, ly, lw, lh, Act::Home);
    }
    cv.resetClip();
    if (!nPins) {
        PlateStyle ps;
        ps.fill = ps.fill2 = col::navy; ps.rim = col::mist; ps.rimAlpha = 0.25f;
        cv.plate(W / 2 - 150, 380, 300, 70, ps);
        cv.text(F(900, 20), "No stops picked yet", W / 2.f, 396, col::white, Align::Center);
        cv.text(F(800, 14), "Pick one on the stops screen", W / 2.f, 424, col::mist, Align::Center);
    }
    // How old the data is, while it can't be refreshed.
    int oldest = -1;
    for (const Section& s : d.sec)
        if (hasPick(s) && staleSec(d, s, c)) oldest = std::max(oldest, ageMin(s, c));
    if (oldest >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Updated %d min ago", oldest);
        const Font* f = F(800, 14);
        const int tw = static_cast<int>(ceilf(Canvas::measure(f, buf))) + 24;
        PlateStyle ps = smallPlate();
        ps.fill = ps.fill2 = col::raised; ps.rim = col::amber; ps.rimAlpha = 0.6f; ps.edge = 4;
        cv.plate(16, H - 20 - 40, tw, 40, ps);
        cv.text(f, buf, 28, H - 20 - 19, col::amber, Align::Left, Base::Middle);
    }
    // Recenter: headlight yellow while the frame is on the picks, like the app's button.
    const bool follow = !a.panX && !a.panY;
    PlateStyle rp = smallPlate();
    rp.fill = rp.fill2 = follow ? col::headlight : col::raised;
    rp.rim = follow ? col::navy : col::white;
    rp.rimAlpha = follow ? 1 : 0.22f;
    rp.edge = 4;
    cv.plate(RECENTER.x, RECENTER.y, RECENTER.w, RECENTER.h, rp);
    cv.ink(kPin, 9, RECENTER.x + RECENTER.w / 2 - 9, RECENTER.y + RECENTER.h / 2 - 9, 2, follow ? col::navy : col::white);
    hit(a, RECENTER.x, RECENTER.y, RECENTER.w, RECENTER.h, Act::Recenter);
}

// ---------- full-screen messages: setup, no key ----------
void drawMessage(Canvas& cv, bool worried, uint32_t ms, const char* title, const char* const* body, int nBody,
                 const char* foot) {
    const int bob = (ms / 500) % 2 ? 0 : -4;
    drawPet(cv, worried ? cta::Mood::Worried : cta::Mood::Happy, false, W / 2 - 64, 196 + bob, 4, cta::Accessory::None, true);
    cv.text(F(900, 34), title, W / 2.f, 380, col::white, Align::Center);
    for (int k = 0; k < nBody; ++k) cv.text(F(800, 18), body[k], W / 2.f, 436 + k * 28, col::mist, Align::Center);
    if (foot) cv.text(F(900, 16), foot, W / 2.f, 436 + nBody * 28 + 24, col::headlight, Align::Center);
}

const Hit* find(const App& a, float x, float y, bool hold) {
    for (int k = a.nHits - 1; k >= 0; --k) {
        const Hit& h = a.hits[k];
        if ((hold ? h.hold : h.tap) == Act::None) continue;
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return &h;
    }
    return nullptr;
}

void openPicker(App& a, int section) {
    a.screen = Screen::Picker;
    a.picker.step = 1;
    a.picker.section = section;
    a.picker.scroll = 0;
}

void act(App& a, const Data& d, const PickerLists& pl, Act what, int arg, uint32_t ms) {
    auto& p = a.picker;
    switch (what) {
        case Act::None: break;
        case Act::Poke: a.poked = true; a.pokeMs = ms; break;
        case Act::MapToggle:
            if (a.screen == Screen::Map) a.screen = Screen::Home;
            else { a.screen = Screen::Map; a.panX = a.panY = 0; }   // the map opens framed on the picks
            break;
        case Act::OpenPicker: openPicker(a, arg); break;
        case Act::Back:
            if (p.step > 1) { --p.step; p.scroll = 0; }
            else a.screen = Screen::Home;
            break;
        case Act::Kind:
            p.bus = arg == 1;
            p.step = 2;
            p.scroll = 0;
            if (p.bus) a.request = Request::Routes;
            break;
        case Act::Line:
            snprintf(p.line, sizeof(p.line), "%s", kLines[arg].route);
            p.lineName[0] = 0;
            p.step = 3;
            break;
        case Act::Route:
            if (arg >= pl.nRoutes) break;
            snprintf(p.line, sizeof(p.line), "%s", pl.routes[arg].rt);
            snprintf(p.lineName, sizeof(p.lineName), "%s", pl.routes[arg].name);
            p.step = 3;
            p.scroll = 0;
            a.request = Request::Dirs;
            break;
        case Act::Dir: {
            const char* dirs[4];
            if (p.bus) {
                if (dirsState(a, pl) != Load::Ready || arg >= pl.nDirs) break;
                snprintf(p.dirName, sizeof(p.dirName), "%s", pl.dirs[arg].name);
                a.request = Request::Stops;
            } else {
                if (arg >= trainDirs(p.line, dirs)) break;
                snprintf(p.dirName, sizeof(p.dirName), "%s", dirs[arg]);
            }
            p.dir = arg;
            p.step = 4;
            p.scroll = 0;
            break;
        }
        case Act::Retry:
            a.request = p.step == 2 ? Request::Routes : p.step == 3 ? Request::Dirs : Request::Stops;
            break;
        case Act::Stop: {
            cta::PickedStop s;
            memset(&s, 0, sizeof(s));
            s.type = p.bus ? cta::Kind::Bus : cta::Kind::Train;
            snprintf(s.route, sizeof(s.route), "%s", p.line);
            snprintf(s.dir, sizeof(s.dir), "%s", p.dirName);
            if (p.bus) {
                if (stopsState(a, pl) != Load::Ready || arg >= pl.nStops) break;
                snprintf(s.id, sizeof(s.id), "%s", pl.stops[arg].id);
                snprintf(s.name, sizeof(s.name), "%s", pl.stops[arg].name);
                snprintf(s.routeName, sizeof(s.routeName), "%.24s", p.lineName);
            } else {
                cta::StationRef refs[64];
                if (arg >= static_cast<int>(cta::trainStations(p.line, p.dirName, refs, 64))) break;
                snprintf(s.id, sizeof(s.id), "%s", refs[arg].line->stopIds[refs[arg].index]);
                snprintf(s.name, sizeof(s.name), "%.24s", refs[arg].line->names[refs[arg].index]);
            }
            a.pick = s;
            a.pickedSection = p.section;
            a.picked = true;
            a.screen = Screen::Home;
            break;
        }
        case Act::Recenter: a.panX = a.panY = 0; break;
        case Act::Home: a.screen = Screen::Home; break;
    }
    (void)d;
}

}  // namespace

// ---------- derived (the logic is cta's dock_status) ----------

bool dueNow(const Section& s, uint32_t now) { return s.pick.id[0] && cta::dueNow(s.rec, now); }

namespace {
// The picked sections' records, each named by its pick (a record not fetched yet is empty).
size_t pickedRecords(const Data& d, cta::StopRecord* out) {
    size_t n = 0;
    for (const Section& s : d.sec) {
        if (!s.pick.id[0]) continue;
        out[n] = s.rec;
        snprintf(out[n].route, sizeof(out[n].route), "%s", s.pick.route);
        out[n].type = s.pick.type;
        ++n;
    }
    return n;
}
}  // namespace

cta::Mood moodOf(const Data& d, const Clock& c) {
    cta::StopRecord recs[cta::kDockStops];
    const size_t n = pickedRecords(d, recs);
    return cta::dockMood(recs, n, c.now, c.night);
}

void reasonText(const Data& d, const Clock& c, char* out, size_t cap) {
    cta::StopRecord recs[cta::kDockStops];
    const size_t n = pickedRecords(d, recs);
    cta::moodReason(recs, n, c.now, c.night, d.wifiSetUp && !d.wifiUp, d.status, out, cap);
}

// ---------- the frame ----------

void draw(Canvas& cv, App& a, const Data& d, const PickerLists& pl, const Clock& c, uint32_t ms) {
    cv.clear(col::bg);
    cv.alpha = 1;
    a.nHits = 0;
    if (!d.keys) {
        const char* body[] = {"This dock was built without", "CTA API keys."};
        drawMessage(cv, true, ms, "No API key", body, 2, "Add them to secrets.h and reflash");
    } else if (!d.wifiSetUp) {
        const char* body[] = {"Open the CTA app on your iPhone", "to set up Wi-Fi."};
        char foot[40];
        snprintf(foot, sizeof(foot), "%s is waiting", d.name);
        drawMessage(cv, false, ms, "Set up Wi-Fi", body, 2, foot);
    } else if (a.screen == Screen::Picker) {
        drawPicker(cv, a, pl, ms);
    } else if (a.screen == Screen::Map) {
        drawMap(cv, a, d, c, ms);
    } else {
        drawHome(cv, a, d, c, ms);
    }
    if (c.night) cv.fillRect(0, 0, W, H, col::bg, 0.62f);   // the backlight is on/off only: night is a black veil
    cv.alpha = 1;
}

// ---------- input ----------

void tick(App& a, uint32_t ms) {
    auto& p = a.press;
    // Signed: a press stamped by the touch task just after this frame's ms is not 4 billion ms old.
    if (!p.down || p.moved || p.held || a.screen != Screen::Home || !cta::longPressDue(p.t0, ms)) return;
    if (const Hit* h = find(a, p.x0, p.y0, true)) {
        p.held = true;
        if (h->hold == Act::OpenPicker) openPicker(a, h->arg);
    }
}

void pointerDown(App& a, float x, float y, uint32_t ms) {
    auto& p = a.press;
    p = {};
    p.down = true;
    p.x0 = p.x = x; p.y0 = p.y = y;
    p.s0 = a.picker.scroll;
    p.panX0 = a.panX; p.panY0 = a.panY;
    p.edge = x < 40;
    p.t0 = ms;
}

void pointerMove(App& a, const PickerLists& pl, float x, float y, uint32_t ms) {
    auto& p = a.press;
    if (!p.down) return;
    p.x = x; p.y = y;
    if (cta::pressMoved(x - p.x0, y - p.y0)) p.moved = true;
    // On the map a drag pans it, except one that starts at the left edge: that swipes back to the stops.
    if (p.moved && a.screen == Screen::Map && !p.edge && p.y0 > STRIP_H) {
        a.panX = p.panX0 + static_cast<int>(lroundf(x - p.x0));
        a.panY = p.panY0 + static_cast<int>(lroundf(y - p.y0));
    }
    if (p.moved && a.screen == Screen::Picker)
        a.picker.scroll = fmaxf(0, fminf(pickerMaxScroll(a, pl), p.s0 - (y - p.y0)));
    (void)ms;
}

void pointerUp(App& a, const Data& d, const PickerLists& pl, float x, float y, uint32_t ms) {
    auto& p = a.press;
    if (!p.down) return;
    p.down = false;
    const cta::Gesture g = cta::classifyRelease(x - p.x0, y - p.y0, p.moved, p.held);
    if (g == cta::Gesture::Tap) {
        if (const Hit* h = find(a, x, y, false)) act(a, d, pl, h->tap, h->arg, ms);
    } else if (g == cta::Gesture::SwipeLeft && a.screen == Screen::Home) {
        act(a, d, pl, Act::MapToggle, 0, ms);                          // swipe left: the map
    } else if (g == cta::Gesture::SwipeRight && a.screen == Screen::Map && p.edge) {
        a.screen = Screen::Home;                                       // swipe right from the edge: the stops
    }
}

bool animating(const App& a) { return a.press.down && a.press.moved; }

}  // namespace dock
