#include "ui.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "alerts_weather.h"
#include "cta_config.h"
#include "display.h"
#include "line_colors.h"
#include "local_time.h"
#include "mood.h"
#include "net.h"
#include "pet_render.h"
#include "picker.h"
#include "plate.h"
#include "state.h"

// The look follows the iPhone app and the handheld (iOS redesign spec, revision 2): night navy, Trackie on a
// cream pixel tile with a speech bubble, and one line-colored pixel-plate card per stop with departure tiles.
// The UI is subject to change (dock plan); every size lives here.
namespace ui {

static constexpr int W = display::kWidth;
static constexpr int kStripH = 116;
static constexpr int kSectionH = (display::kHeight - kStripH) / 3;   // 228
static constexpr int kCardX = 16, kCardY = 6, kCardW = W - 32, kCardH = 206;
static constexpr int kTileY = 104, kTileH = 88, kTileGap = 12;
static constexpr int kTileW = (kCardW - 32 - 2 * kTileGap) / 3;
static constexpr int kPetScale = 2;                           // 64 px (spec 7.3)
static constexpr int kTileX = 16, kTileTop = 8, kPetTile = 92;   // the cream tile in the strip
static constexpr uint32_t kTwoFingerMs = 1500;

// ---------- snapshot of gState for one frame ----------

struct View {
    cta::DeviceConfig cfg;
    cta::StopRecord recs[3];
    bool wifiUp, hasWifiConfig, keyMissing, ctaError, hasWeather, timeValid;
    int wmo;
    float tempC;
    uint32_t passkey;
};

static View snapshot() {
    Lock l;
    View v;
    v.cfg = gState.cfg;
    memcpy(v.recs, gState.recs, sizeof v.recs);
    v.wifiUp = gState.wifiUp;
    v.hasWifiConfig = gState.hasWifiConfig;
    v.keyMissing = gState.keyMissing;
    v.ctaError = gState.ctaError;
    v.hasWeather = gState.hasWeather;
    v.timeValid = gState.timeValid;
    v.wmo = gState.wmo;
    v.tempC = gState.tempC;
    v.passkey = gState.passkey;
    return v;
}

// ---------- helpers ----------

static lv_color_t rgb(uint32_t c) { return lv_color_hex(c); }

static uint32_t* psram(size_t pixels) {
    return (uint32_t*)heap_caps_calloc(pixels, 4, MALLOC_CAP_SPIRAM);
}

static lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int x, int y, int w) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, rgb(color), 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    lv_label_set_text(l, "");
    return l;
}

static void setText(lv_obj_t* l, const char* text) {
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

static int textW(const char* text, const lv_font_t* font) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

static lv_obj_t* canvas(lv_obj_t* parent, uint32_t* buf, int x, int y, int w, int h) {
    lv_obj_t* c = lv_canvas_create(parent);
    lv_canvas_set_buffer(c, buf, w, h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(c, x, y);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void clear(pet::Canvas c) { memset(c.px, 0, (size_t)c.w * c.h * 4); }

static uint32_t darker(uint32_t c, int pct) {
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8) out |= ((((c >> s) & 0xFF) * (100 - pct) / 100) << s);
    return out;
}

// A train's line name ("Brown"), or the bus route ("22").
static void routeName(const char* route, char* out, size_t cap) {
    const cta::TrainLineInfo* l = cta::trainLine(route);
    if (!l) {
        strlcpy(out, route, cap);
        return;
    }
    strlcpy(out, l->name, cap);
    char* sp = strchr(out, ' ');
    if (sp) *sp = '\0';
}

// Minutes now, counting down from the fetch (records hold minutes at fetch time).
static int minutesNow(const cta::Arrival& a, uint32_t ts, uint32_t now) {
    int m = a.min - (int)((now > ts ? now - ts : 0) / 60);
    return m < 0 ? 0 : m;
}

static void arrowCircle(pet::Canvas c, int cx, int cy, int r, uint32_t fg, uint32_t ink) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && py >= 0 && px < c.w && py < c.h) c.px[py * c.w + px] = 0xFF000000u | fg;
            }
    auto dot = [&](int x, int y) {
        for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++) c.px[(cy + y + dy) * c.w + cx + x + dx] = 0xFF000000u | ink;
    };
    for (int x = -r / 2; x <= r / 2 - 1; x++) dot(x, -1);   // shaft
    for (int k = 0; k <= r / 2 - 1; k++) {                    // head
        dot(r / 2 - 1 - k, -1 - k);
        dot(r / 2 - 1 - k, -1 + k);
    }
}

// ---------- the status strip ----------

static lv_obj_t* sStrip;
static pet::Canvas sStripPx;
static lv_obj_t* sStripCanvas;
static lv_obj_t *sBubbleName, *sBubbleWord, *sTemp;
static pet::Canvas sPetPx;   // the tile's inside: Trackie, his umbrella, the floating extra
static lv_obj_t* sPetCanvas;
static constexpr int kPetCanvasW = kPetTile - 8, kPetCanvasH = kPetTile - 8;

static int sDebugState = -1;   // two-finger long press cycles moods and states (spec 6.3); -1 = live
static uint32_t sTwoFingerSince = 0;
static bool sTwoFingerFired = false;

static void drawStrip(const View& v, pet::PetState state, bool night) {
    clear(sStripPx);
    // the cream tile, and the bubble beside it (its tail points at Trackie)
    pet::PlateStyle tile{cta::kCream, cta::kCream, cta::kNavy, 255, 3, 5};
    pet::drawPlate(sStripPx, kTileX, kTileTop, kPetTile, kPetTile, tile);
    int bx = kTileX + kPetTile + 18, bw = 176, bh = 56, by = kTileTop + kPetTile / 2 - bh / 2;
    pet::PlateStyle bubble{cta::kCream, cta::kCream, cta::kNavy, 255, 2, 4, pet::kShapeSmall};
    pet::drawPlate(sStripPx, bx, by, bw, bh, bubble);
    pet::fillShape(sStripPx, bx - 6, by + bh / 2 - 6, 10, 4, pet::Shape{1, {0}, 0}, cta::kCream);
    pet::fillShape(sStripPx, bx - 2, by + bh / 2 - 8, 6, 12, pet::Shape{1, {0}, 0}, cta::kCream);
    pet::fillShape(sStripPx, bx - 6, by + bh / 2 - 2, 4, 4, pet::Shape{1, {0}, 0}, cta::kCream);
    // right: the time in the pixel digits, then weather and Wi-Fi
    uint32_t ink = night ? cta::kMist : 0xFFFFFF;
    if (v.timeValid) {
        time_t t = time(nullptr);
        struct tm lt;
        localtime_r(&t, &lt);
        char clock[8];
        snprintf(clock, sizeof clock, "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
        pet::drawText(sStripPx, clock, ink, 4, W - 16 - pet::textWidth(clock, 4), 22);
    }
    int iy = 72;
    uint32_t wifiColor = v.wifiUp ? 0x5C6D93 : 0xFF3B30;   // spec 7.4: the Wi-Fi icon turns red
    pet::drawMono(sStripPx, pet::kGlyphWifi, wifiColor, 3, W - 16 - 21, iy);
    if (v.hasWeather) {
        uint32_t wcol = v.wmo <= 1 ? cta::kHeadlight : cta::kMist;
        pet::drawMono(sStripPx, pet::weatherIcon(v.wmo), wcol, 3, W - 16 - 21 - 96, iy - 3);
        char temp[8];
        snprintf(temp, sizeof temp, "%d°", (int)lroundf(v.tempC * 9 / 5 + 32));
        setText(sTemp, temp);
        lv_obj_remove_flag(sTemp, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(sTemp, LV_OBJ_FLAG_HIDDEN);   // no location yet: no weather (spec 7.5)
    }
    lv_obj_invalidate(sStripCanvas);
    char name[cta::kPetNameLen + 1];
    strlcpy(name, v.cfg.petName, sizeof name);
    for (char* p = name; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    setText(sBubbleName, name);
    setText(sBubbleWord, pet::moodWord(state));
}

static void drawPet(const View& v, pet::PetState state, bool up) {
    clear(sPetPx);
    cta::Accessory acc = v.hasWeather ? cta::accessoryFor(v.wmo, v.tempC) : cta::Accessory::None;
    int bob = up ? -kPetScale : 0;   // one art pixel, twice a second, like the app
    int px = (kPetCanvasW - 32 * kPetScale) / 2, py = 6 * kPetScale + 2;
    pet::drawPet(sPetPx, state, acc, v.cfg.petColors, kPetScale, px, py + bob);
    const pet::Sprite& extra = pet::kExtras[(int)state];
    if (extra.px) {   // floats the other way
        uint32_t pal[pet::kPaletteSize];
        memcpy(pal, pet::kPalette, sizeof pal);
        pal[pet::kSlotBody] = v.cfg.petColors[0];
        pal[pet::kSlotLight] = v.cfg.petColors[1];
        pet::drawSprite(sPetPx, extra, pal, kPetScale, kPetCanvasW - 7 * kPetScale, (up ? 0 : 2));
    }
    lv_obj_invalidate(sPetCanvas);
}

// ---------- stop sections ----------

struct Section {
    lv_obj_t* root;
    pet::Canvas px;
    lv_obj_t* canvas;
    lv_obj_t *route, *stop, *fresh, *dir, *empty, *note;
    lv_obj_t *dest[3], *unit[3], *chip[3];
    pet::Canvas live[3];
    lv_obj_t* liveCanvas[3];
    char sig[200];   // what's drawn, to skip redraws
    bool liveOn[3];
    bool stale;      // stale cards keep the live glyph still (handheld mockup)
};
static Section sSec[3];

static void onSection(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    bool empty;
    {
        Lock l;
        empty = !gState.cfg.stops[i].id[0];
    }
    // An empty section opens on a tap; a filled one on a long press (spec 7.2).
    if (code == LV_EVENT_LONG_PRESSED || (code == LV_EVENT_SHORT_CLICKED && empty)) picker::open(i);
}

static void drawSection(int i, const View& v, uint32_t now, bool night) {
    Section& s = sSec[i];
    const cta::PickedStop& p = v.cfg.stops[i];
    const cta::StopRecord& r = v.recs[i];
    uint32_t age = r.ts && now > r.ts ? now - r.ts : 0;
    bool offline = !v.wifiUp || v.ctaError;
    // At night arrivals come every 5 min (spec 6.6), so a healthy card is only stale past that cadence.
    uint32_t limit = night ? cta::kSlowPollSec + cta::kLedStaleSec : cta::kLedStaleSec;
    bool stale = r.ts && (offline || age > limit);
    int ageMin = (int)(age / 60);

    char sig[sizeof s.sig];
    int n = snprintf(sig, sizeof sig, "%s|%s|%d|%u|%d|%d|%d|%d|", p.id, p.route, r.count, (unsigned)r.ts, r.alert, stale,
                     ageMin, offline);
    for (int k = 0; k < r.count && k < 3 && n < (int)sizeof sig; k++) {
        const cta::Arrival& a = r.arrivals[k];
        n += snprintf(sig + n, sizeof sig - n, "%d%d%d%.8s;", minutesNow(a, r.ts, now), a.live, a.delayed, a.dest);
    }
    if (!strcmp(sig, s.sig)) return;
    strcpy(s.sig, sig);
    s.stale = stale;

    clear(s.px);
    for (int k = 0; k < 3; k++) {
        lv_obj_add_flag(s.dest[k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s.unit[k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s.chip[k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s.liveCanvas[k], LV_OBJ_FLAG_HIDDEN);
        s.liveOn[k] = false;
    }
    if (!p.id[0]) {   // empty: a raised navy plate and the prompt
        pet::PlateStyle plate{cta::kRaised, cta::kRaised};
        pet::drawPlate(s.px, kCardX, kCardY, kCardW, kCardH, plate);
        for (lv_obj_t* o : {s.route, s.stop, s.fresh, s.dir, s.note}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s.empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s.root, LV_OPA_COVER, 0);
        lv_obj_invalidate(s.canvas);
        return;
    }
    lv_obj_add_flag(s.empty, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t* o : {s.route, s.stop, s.dir}) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);

    uint32_t line = cta::accent(p.route);
    uint32_t fill = cta::card(line), top = cta::cardTop(line), text = cta::cardText(line);
    pet::PlateStyle card{top, fill, text, 64};
    pet::drawPlate(s.px, kCardX, kCardY, kCardW, kCardH, card);

    char name[16];
    routeName(p.route, name, sizeof name);
    for (lv_obj_t* o : {s.route, s.stop, s.dir, s.note}) lv_obj_set_style_text_color(o, rgb(text), 0);
    setText(s.route, name);
    setText(s.stop, p.name[0] ? p.name : r.name);
    setText(s.dir, p.dir);
    if (r.alert) {   // ⚠ after the route name
        int ax = kCardX + 16 + textW(name, &lv_font_montserrat_48) + 12;
        pet::drawMono(s.px, pet::kGlyphAlert, text, 2, ax, kCardY + 18);
    }
    arrowCircle(s.px, kCardX + 26, kCardY + 78, 10, text, fill);

    if (stale && ageMin >= 1) {
        char buf[32];
        snprintf(buf, sizeof buf, cta::kCopyUpdated, ageMin);
        setText(s.fresh, buf);
        lv_obj_remove_flag(s.fresh, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s.fresh, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_opa(s.root, stale ? 150 : LV_OPA_COVER, 0);   // stale cards recede (spec 7.4)

    if (!r.ts || r.count == 0) {
        setText(s.note, !r.ts ? (offline ? "Waiting for data" : "Loading...") : "No arrivals right now");
        lv_obj_remove_flag(s.note, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s.canvas);
        return;
    }
    lv_obj_add_flag(s.note, LV_OBJ_FLAG_HIDDEN);

    // the next 3 departures as tiles; the first outlined in the light shade, like the app's stop detail
    for (int k = 0; k < r.count && k < 3; k++) {
        const cta::Arrival& a = r.arrivals[k];
        int tx = kCardX + 16 + k * (kTileW + kTileGap), ty = kCardY + kTileY;
        pet::PlateStyle tile{darker(fill, 10), darker(fill, 30), text, (uint8_t)(k == 0 ? 255 : 40), 2, 3,
                             pet::kShapeSmall};
        pet::drawPlate(s.px, tx, ty, kTileW, kTileH, tile);
        int m = minutesNow(a, r.ts, now);
        char digits[8];
        if (m < 1) strcpy(digits, "Due");
        else snprintf(digits, sizeof digits, "%d", m > 99 ? 99 : m);
        uint32_t ink = a.live ? text : darker(text, 35);   // timetable-only times are dimmed (spec 6.2)
        pet::drawText(s.px, digits, ink, 5, tx + 12, ty + 10);
        if (m >= 1) {
            lv_obj_set_pos(s.unit[k], tx + 12 + pet::textWidth(digits, 5) + 6, ty + 26);
            lv_obj_set_style_text_color(s.unit[k], rgb(ink), 0);
            lv_obj_remove_flag(s.unit[k], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_text_color(s.dest[k], rgb(text), 0);
        setText(s.dest[k], a.dest);
        lv_obj_remove_flag(s.dest[k], LV_OBJ_FLAG_HIDDEN);
        if (a.delayed || !a.live) {   // amber Delayed, dimmed Sched (spec 7.1)
            const char* chip = a.delayed ? "Delayed" : "Sched";
            int cw = textW(chip, &lv_font_montserrat_14) + 12;
            pet::fillShape(s.px, tx + 12, ty + 62, cw, 18, pet::kShapeChip, a.delayed ? cta::kAmber : darker(fill, 45));
            setText(s.chip[k], chip);
            lv_obj_set_style_text_color(s.chip[k], rgb(a.delayed ? 0x000000 : ink), 0);
            lv_obj_set_pos(s.chip[k], tx + 18, ty + 63);
            lv_obj_remove_flag(s.chip[k], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(s.dest[k], ty + 44);
        } else {
            lv_obj_set_y(s.dest[k], ty + 56);
        }
        if (a.live) {
            lv_obj_set_pos(s.liveCanvas[k], tx + kTileW - 26, ty + 10);
            lv_obj_remove_flag(s.liveCanvas[k], LV_OBJ_FLAG_HIDDEN);
            s.liveOn[k] = true;
            clear(s.live[k]);
            pet::drawMono(s.live[k], pet::kGlyphLive0, text, 2, 0, 0);
            lv_obj_invalidate(s.liveCanvas[k]);
        }
    }
    lv_obj_invalidate(s.canvas);
}

// The live glyph's second frame, blinking twice a second (the app's live glyph).
static void blinkLive(int i, bool frame, uint32_t text) {
    Section& s = sSec[i];
    for (int k = 0; k < 3; k++) {
        if (!s.liveOn[k] || s.stale) continue;
        clear(s.live[k]);
        pet::drawMono(s.live[k], frame ? pet::kGlyphLive1 : pet::kGlyphLive0, text, 2, 0, 0);
        lv_obj_invalidate(s.liveCanvas[k]);
    }
}

static void buildSection(int i) {
    Section& s = sSec[i];
    s.root = box(lv_screen_active(), 0, kStripH + i * kSectionH, W, kSectionH);
    lv_obj_add_flag(s.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.root, onSection, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(s.root, onSection, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    s.px = {psram((size_t)W * kSectionH), W, kSectionH};
    s.canvas = canvas(s.root, s.px.px, 0, 0, W, kSectionH);
    int L = kCardX + 16;
    s.route = label(s.root, &lv_font_montserrat_48, 0xFFFFFF, L, kCardY + 8, 0);
    s.stop = label(s.root, &lv_font_montserrat_20, 0xFFFFFF, W / 2 + 20, kCardY + 16, kCardW / 2 - 52);
    lv_obj_set_style_text_align(s.stop, LV_TEXT_ALIGN_RIGHT, 0);
    s.fresh = label(s.root, &lv_font_montserrat_14, cta::kAmber, W / 2 + 20, kCardY + 44, kCardW / 2 - 52);
    lv_obj_set_style_text_align(s.fresh, LV_TEXT_ALIGN_RIGHT, 0);
    s.dir = label(s.root, &lv_font_montserrat_24, 0xFFFFFF, L + 26, kCardY + 66, kCardW - 70);
    s.note = label(s.root, &lv_font_montserrat_20, 0xFFFFFF, L, kCardY + kTileY + 30, kCardW - 32);
    s.empty = label(s.root, &lv_font_montserrat_24, cta::kMist, 0, kCardY + kCardH / 2 - 14, W);
    lv_obj_set_style_text_align(s.empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s.empty, cta::kCopyEmptyStop);
    for (int k = 0; k < 3; k++) {
        int tx = kCardX + 16 + k * (kTileW + kTileGap);
        s.dest[k] = label(s.root, &lv_font_montserrat_14, 0xFFFFFF, tx + 12, kCardY + kTileY + 56, kTileW - 20);
        s.unit[k] = label(s.root, &lv_font_montserrat_14, 0xFFFFFF, tx, kCardY + kTileY + 26, 0);
        lv_label_set_text(s.unit[k], "min");
        s.chip[k] = label(s.root, &lv_font_montserrat_14, 0x000000, tx, 0, 0);
        s.live[k] = {psram(16 * 16), 16, 16};
        s.liveCanvas[k] = canvas(s.root, s.live[k].px, tx, 0, 16, 16);
    }
    s.sig[0] = '\0';
}

// ---------- full-screen states ----------

static lv_obj_t *sMessage, *sMessageText, *sMessageSub;
static pet::Canvas sMessagePx;
static lv_obj_t* sMessageCanvas;
static lv_obj_t* sDim;
static int sMessageKind = -1;

static void showMessage(int kind, const View& v, pet::PetState state) {
    // kind: 0 none, 1 no Wi-Fi set up, 2 no API key, 3 BLE passkey
    if (kind == sMessageKind && kind != 3) return;
    sMessageKind = kind;
    if (!kind) {
        lv_obj_add_flag(sMessage, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(sMessage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(sMessage);   // above the picker
    clear(sMessagePx);
    if (kind == 3) {
        char code[8];
        snprintf(code, sizeof code, "%06u", (unsigned)(v.passkey % 1000000));
        pet::drawText(sMessagePx, code, cta::kHeadlight, 10, (W - pet::textWidth(code, 10)) / 2, 60);
        setText(sMessageText, "Pairing with your iPhone");
        setText(sMessageSub, "Type this code when iOS asks for it");
    } else {
        pet::PlateStyle tile{cta::kCream, cta::kCream, cta::kNavy, 255, 3, 5};
        int t = 200, x = (W - t) / 2;
        pet::drawPlate(sMessagePx, x, 0, t, t, tile);
        pet::drawPet(sMessagePx, state, cta::Accessory::None, v.cfg.petColors, 5, x + (t - 160) / 2, 24);
        setText(sMessageText, kind == 1 ? cta::kCopyNoWifi : cta::kCopyNoKey);
        setText(sMessageSub, kind == 1 ? "Gear > Devices > Dock" : "Add your CTA keys to firmware/secrets.h");
    }
    lv_obj_invalidate(sMessageCanvas);
}

// ---------- the frame loop ----------

static uint32_t sLastDraw = 0, sLastPet = 0;
static bool sPetUp = false;
static char sStripSig[80];

static void twoFingerDebug() {
    lv_indev_t* in = lv_indev_get_next(nullptr);
    lv_point_t pt = {0, 0};
    if (in) lv_indev_get_point(in, &pt);
    bool onPet = pt.x >= kTileX && pt.x < kTileX + kPetTile && pt.y >= kTileTop && pt.y < kTileTop + kPetTile + 20;
    if (display::touchCount() >= 2 && onPet) {
        if (!sTwoFingerSince) sTwoFingerSince = millis();
        if (!sTwoFingerFired && millis() - sTwoFingerSince > kTwoFingerMs) {
            sDebugState = sDebugState >= (int)pet::PetState::Asleep ? -1 : sDebugState + 1;
            sTwoFingerFired = true;
            sStripSig[0] = '\0';
        }
    } else {
        sTwoFingerSince = 0;
        sTwoFingerFired = false;
    }
}

void begin() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, rgb(cta::kNavy), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    sStrip = box(scr, 0, 0, W, kStripH);
    sStripPx = {psram((size_t)W * kStripH), W, kStripH};
    sStripCanvas = canvas(sStrip, sStripPx.px, 0, 0, W, kStripH);
    sPetPx = {psram((size_t)kPetCanvasW * kPetCanvasH), kPetCanvasW, kPetCanvasH};
    sPetCanvas = canvas(sStrip, sPetPx.px, kTileX + 4, kTileTop + 4, kPetCanvasW, kPetCanvasH);
    int bx = kTileX + kPetTile + 18 + 13;
    sBubbleName = label(sStrip, &lv_font_montserrat_14, 0x5B6683, bx, kTileTop + kPetTile / 2 - 22, 150);
    sBubbleWord = label(sStrip, &lv_font_montserrat_20, cta::kNavy, bx, kTileTop + kPetTile / 2 - 4, 150);
    sTemp = label(sStrip, &lv_font_montserrat_20, cta::kMist, W - 16 - 21 - 64, 70, 0);

    for (int i = 0; i < 3; i++) buildSection(i);

    sMessage = box(lv_layer_top(), 0, 0, W, display::kHeight);
    lv_obj_set_style_bg_color(sMessage, rgb(cta::kNavy), 0);
    lv_obj_set_style_bg_opa(sMessage, LV_OPA_COVER, 0);
    lv_obj_add_flag(sMessage, LV_OBJ_FLAG_CLICKABLE);   // swallows touches underneath
    sMessagePx = {psram((size_t)W * 220), W, 220};
    sMessageCanvas = canvas(sMessage, sMessagePx.px, 0, 200, W, 220);
    sMessageText = label(sMessage, &lv_font_montserrat_28, 0xFFFFFF, 24, 450, W - 48);
    lv_label_set_long_mode(sMessageText, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(sMessageText, LV_TEXT_ALIGN_CENTER, 0);
    sMessageSub = label(sMessage, &lv_font_montserrat_20, cta::kMist, 24, 540, W - 48);
    lv_label_set_long_mode(sMessageSub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(sMessageSub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(sMessage, LV_OBJ_FLAG_HIDDEN);

    // Software dimming: the 4.3" board's backlight is on/off only, so a black veil dims the screen.
    sDim = box(lv_layer_sys(), 0, 0, W, display::kHeight);
    lv_obj_set_style_bg_color(sDim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sDim, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(sDim, LV_OBJ_FLAG_CLICKABLE);
    picker::begin();
}

void tick() {
    uint32_t ms = millis();
    twoFingerDebug();
    if (ms - sLastDraw < 200) return;   // 5 Hz is plenty for data; the pet steps at 2 Hz
    sLastDraw = ms;
    View v = snapshot();
    uint32_t now = net::now();
    bool night = v.timeValid && cta::inNightHours(net::minuteOfDay(), v.cfg.nightStart, v.cfg.nightEnd);

    bool eligible[3] = {true, true, true};   // on the dock all 3 stops can make the pet Hyped (spec 6.3)
    cta::StopRecord picked[3];
    size_t n = 0;
    for (int i = 0; i < 3; i++)
        if (v.cfg.stops[i].id[0]) picked[n++] = v.recs[i];
    cta::Mood mood = cta::deviceMood(picked, eligible, n, now, night, true);
    pet::PetState state = sDebugState >= 0 ? (pet::PetState)sDebugState : pet::petStateFor(mood, false);

    int kind = v.passkey ? 3 : !v.hasWifiConfig ? 1 : v.keyMissing ? 2 : 0;
    showMessage(kind, v, state);

    char sig[sizeof sStripSig];
    snprintf(sig, sizeof sig, "%d|%d|%d|%d|%.1f|%s|%u|%d", (int)state, v.wifiUp, v.hasWeather, v.wmo, v.tempC,
             v.cfg.petName, (unsigned)(v.timeValid ? now / 60 : 0), night);
    if (strcmp(sig, sStripSig)) {
        strcpy(sStripSig, sig);
        drawStrip(v, state, night);
        sLastPet = 0;
    }
    if (ms - sLastPet >= 500) {
        sLastPet = ms;
        sPetUp = !sPetUp;
        drawPet(v, state, sPetUp);
        for (int i = 0; i < 3; i++)
            if (v.cfg.stops[i].id[0]) blinkLive(i, sPetUp, cta::cardText(cta::accent(v.cfg.stops[i].route)));
    }
    for (int i = 0; i < 3; i++) drawSection(i, v, now, night);

    int veil = (100 - v.cfg.screen) * 2;   // screen brightness from the app
    if (night && veil < 170) veil = 170;   // night hours dim the screen (spec 7.4)
    lv_opa_t opa = veil > 220 ? 220 : veil;
    if (lv_obj_get_style_bg_opa(sDim, 0) != opa) lv_obj_set_style_bg_opa(sDim, opa, 0);   // else it repaints the whole screen
}

}  // namespace ui
