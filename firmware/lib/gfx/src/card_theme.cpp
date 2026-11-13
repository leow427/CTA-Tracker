#include "card_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fonts.h"
#include "line_colors.h"
#include "pet_render.h"
#include "pet_sprites.h"

namespace ui {

namespace {
const Font* F(int w, int s) { return Canvas::font(w, s); }
}  // namespace

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

const Theme& th(const char* route, bool bus) {
    static Theme cache[32];
    static int n = 0;
    for (int i = 0; i < n; ++i)
        if (cache[i].bus == bus && strcmp(cache[i].route, route) == 0) return cache[i];
    Theme& t = cache[n < 32 ? n++ : 31];
    snprintf(t.route, sizeof(t.route), "%s", route);
    t.bus = bus;
    if (bus) {
        t.accent = kBusOutline;
        t.top = kBusTop; t.card = kBusCard; t.text = kBusText; t.outline = kBusOutline; t.halo = kBusText;
        t.tile = 0xFFFFFF; t.tile2 = 0xF6F9FB;
        return t;
    }
    const uint32_t a = cta::lineColor(route);
    t.accent = a;
    t.top = shaded(a, 0.09, 0.95);
    t.card = shaded(a, 0.04, 0.85);
    t.text = lifted(a, t.top, 4.5, 3);
    t.outline = t.halo = 0;
    t.tile = shaded(a, 0.03, 0.7);
    t.tile2 = shaded(a, 0.015, 0.6);
    return t;
}

// ---------- text and icons in a card's colors, outlined on a bus (the app's OutlinedText) ----------
float cardText(Canvas& cv, const Font* f, const char* s, float x, float y, const Theme& t, Align al, Base b) {
    if (!t.outline) return cv.text(f, s, x, y, t.text, al, b);
    return cv.textOutlined(f, s, x, y, t.text, t.outline, f->size >= 20 ? 2 : 1, al, b);
}
int cardDigits(Canvas& cv, const char* s, float x, int y, int scale, const Theme& t, Align al) {
    if (t.outline) cv.pixelText(s, x, y, scale, t.outline, al, 2);
    return cv.pixelText(s, x, y, scale, t.text, al);
}
void cardArrow(Canvas& cv, float cx, float cy, float r, const Theme& t) {
    if (t.outline) cv.fillCircle(cx, cy, r + 1.5f, t.outline);
    cv.arrowCircle(cx, cy, r, t.text, t.card);
}

// ---------- glyphs ----------
const char* const kLive[2][8] = {{".......K", ".....K..", "..KK.K.K", ".KKKK.K.", ".KKKK.K.", "..KK.K.K", ".....K..", ".......K"},
                                 {"........", "........", "..KK....", ".KKKK...", ".KKKK...", "..KK....", "........", "........"}};
const char* const kAlert[16] = {".......KK.......", ".......KK.......", "......KKKK......", "......KKKK......", ".....KKKKKK.....",
                                ".....KK..KK.....", "....KKK..KKK....", "....KKK..KKK....", "...KKKK..KKKK...", "...KKKK..KKKK...",
                                "..KKKKKKKKKKKK..", "..KKKKKKKKKKKK..", ".KKKKKK..KKKKKK.", ".KKKKKK..KKKKKK.", "KKKKKKKKKKKKKKKK",
                                "KKKKKKKKKKKKKKKK"};
namespace {
struct Icon { const char* name; const char* rows[7]; int n; };
const Icon kIcons[] = {
    {"sun", {"..x..", "x.x.x", ".xxx.", "xxxxx", ".xxx.", "x.x.x", "..x.."}, 7},
    {"cloud", {".......", "..xx...", ".xxxxx.", "xxxxxxx", ".......", ".x.x.x.", "x.x.x.."}, 7},
    {"snow", {"x.x.x", ".xxx.", "xx.xx", ".xxx.", "x.x.x"}, 5},
    {"wifi", {".xxxxx.", "x.....x", "..xxx..", ".x...x.", "...x..."}, 5},
    {"phone", {"xxx", "x.x", "x.x", "x.x", "xxx"}, 5},
    {"off", {"x...x", ".x.x.", "..x..", ".x.x.", "x...x"}, 5},
    {"plus", {"..x..", "..x..", "xxxxx", "..x..", "..x.."}, 5},
    {"map", {"xx..x..xx", "x.xx.xx.x", "x.x..x..x", "x.x..x..x", "x.x..x..x", "x.xx.xx.x", "xx..x..xx"}, 7},   // a folded map
    {"cards", {"xxxxxxxxx", "x.......x", "xxxxxxxxx", ".........", "xxxxxxxxx", "x.......x", "xxxxxxxxx"}, 7},   // two cards
};
}  // namespace
void icon(Canvas& cv, const char* name, float cx, float cy, int s, uint32_t rgb) {
    for (const Icon& ic : kIcons)
        if (strcmp(ic.name, name) == 0) {
            const int w = static_cast<int>(strlen(ic.rows[0])), h = ic.n;
            cv.ink(ic.rows, h, static_cast<int>(lroundf(cx - w * s / 2.f)), static_cast<int>(lroundf(cy - h * s / 2.f)), s, rgb);
        }
}
void inkRing(Canvas& cv, const char* const* rows, int n, int x, int y, int s, uint32_t rgb, uint32_t ring) {
    if (ring) {
        cv.ink(rows, n, x + s, y, s, ring);
        cv.ink(rows, n, x - s, y, s, ring);
        cv.ink(rows, n, x, y + s, s, ring);
        cv.ink(rows, n, x, y - s, s, ring);
    }
    cv.ink(rows, n, x, y, s, rgb);
}
void liveGlyph(Canvas& cv, int x, int y, bool live, bool blinks, uint32_t ms, uint32_t halo) {
    inkRing(cv, kLive[live && blinks && (ms / 500) % 2 ? 1 : 0], 8, x, y, 2, live ? kLiveInk : kSchedInk, halo);
}
void chevron(Canvas& cv, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb) {
    cv.line(x0, y0, x1, y1, 3, rgb);
    cv.line(x1, y1, x2, y2, 3, rgb);
}

// ---------- Trackie, via the shared pet renderer ----------
void drawPet(Canvas& cv, cta::Mood mood, bool docked, int x, int y, int s, cta::Accessory acc, bool extra) {
    const pet::PetState st = pet::petStateFor(mood, docked);
    auto put = [&](const pet::Sprite& sp) { cv.sprite(sp.idx, sp.w, sp.h, sp.dx, sp.dy, x, y, pet::kDefaultPalette565, s); };
    put(pet::kPet[static_cast<int>(st)]);
    if (const pet::Sprite* ov = pet::overlayFor(st, acc)) put(*ov);
    if (extra && pet::kPetExtra[static_cast<int>(st)]) put(*pet::kPetExtra[static_cast<int>(st)]);
}

// ---------- the speech bubble ----------
float bubbleWidth(const char* text) { return ceilf(fminf(170, Canvas::measure(F(900, 20), text))) + 26; }

void bubble(Canvas& cv, const char* text, int x, float cy, const char* label) {
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

}  // namespace ui
