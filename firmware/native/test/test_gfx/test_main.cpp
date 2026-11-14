// The canvas calls added for the dock's map (gfx2d seg and dot), against the mockup's definitions; and the dock's frame
// recording (ui::Recording), against drawing directly.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "gfx2d.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr int N = 32;
uint16_t px[N * N];
constexpr uint16_t kWhite = 0xFFFF;
bool on(int x, int y) { return px[y * N + x] != 0; }
int count() {
    int n = 0;
    for (uint16_t p : px) n += p != 0;
    return n;
}
}  // namespace

static void test_dot_knocks_off_corners() {
    memset(px, 0, sizeof(px));
    ui::Canvas cv(px, N, N);
    cv.dot(10, 10, 7, 0xFFFFFF);   // covers x, y in [7, 14) less the four corners
    TEST_ASSERT_EQUAL_INT(7 * 7 - 4, count());
    TEST_ASSERT_TRUE(on(8, 7));
    TEST_ASSERT_TRUE(on(7, 8));
    TEST_ASSERT_FALSE(on(7, 7));
    TEST_ASSERT_FALSE(on(13, 13));
    TEST_ASSERT_FALSE(on(13, 7));
    TEST_ASSERT_TRUE(on(13, 12));
    TEST_ASSERT_EQUAL_HEX16(kWhite, px[10 * N + 10]);
    cv.dot(10.4f, 20.6f, 5, 0xFFFFFF);   // rounded to (10, 21)
    TEST_ASSERT_TRUE(on(10, 21));
    TEST_ASSERT_FALSE(on(8, 19));
    TEST_ASSERT_TRUE(on(9, 19));
}

static void test_seg_blocks_along_a_line() {
    memset(px, 0, sizeof(px));
    ui::Canvas cv(px, N, N);
    cv.seg(4, 4, 20, 4, 3, 0xFFFFFF);   // horizontal: rows 3..5, columns 3..21
    TEST_ASSERT_EQUAL_INT(3 * 19, count());
    TEST_ASSERT_TRUE(on(3, 3));
    TEST_ASSERT_TRUE(on(21, 5));
    TEST_ASSERT_FALSE(on(22, 4));
    TEST_ASSERT_FALSE(on(4, 6));
    memset(px, 0, sizeof(px));
    cv.seg(2, 2, 12, 12, 2, 0xFFFFFF);   // a diagonal of 2 × 2 blocks: each step's block at (x - 1, y - 1)
    for (int k = 2; k <= 12; ++k) {
        TEST_ASSERT_TRUE(on(k - 1, k - 1));
        TEST_ASSERT_TRUE(on(k, k));
    }
    TEST_ASSERT_FALSE(on(12, 2));
    memset(px, 0, sizeof(px));
    cv.seg(5, 5, 5, 5, 4, 0xFFFFFF);   // one point: one block
    TEST_ASSERT_EQUAL_INT(16, count());
}

static void test_seg_fades_like_overlapping_rects() {
    // The mockup fills each block with the alpha in turn, so where blocks overlap the line builds up: a pixel inside
    // a long horizontal line of 3 px blocks is covered 3 times, more opaque than its ends.
    memset(px, 0, sizeof(px));
    ui::Canvas cv(px, N, N);
    cv.seg(4, 10, 20, 10, 3, 0xFFFFFF, 0.3f);
    const uint16_t mid = px[10 * N + 12], end = px[10 * N + 3];
    TEST_ASSERT_TRUE((mid >> 11) > (end >> 11));
    TEST_ASSERT_TRUE(end != 0);
}

static void test_seg_clipped() {
    memset(px, 0, sizeof(px));
    ui::Canvas cv(px, N, N);
    cv.seg(-100, 5, 200, 5, 3, 0xFFFFFF);   // runs far off both sides: only on-canvas pixels change
    TEST_ASSERT_EQUAL_INT(3 * N, count());
}

static void test_strided_matches_row_major() {
    // A 32 x 32 screen drawn in two 16-row bands laid out a quarter turn right (the dock's panel order, x across
    // a band's rows), compared with the same drawing row-major.
    memset(px, 0, sizeof(px));
    ui::Canvas plain(px, N, N);
    auto scene = [](ui::Canvas& cv) {
        cv.clear(0x102030);
        cv.seg(2, 3, 29, 27, 3, 0xFFFFFF, 0.5f);
        cv.dot(16, 16, 7, 0xFF0000);
        cv.fillCircle(8, 24, 5, 0x00FF00);
    };
    scene(plain);
    static uint16_t band[N * 16];
    for (int y0 = 0; y0 < N; y0 += 16) {
        ui::Canvas cv = ui::Canvas::strided(band, (N - 1) * 16 - y0, -16, 1, 0, y0, N, y0 + 16);
        scene(cv);
        for (int y = y0; y < y0 + 16; ++y)
            for (int x = 0; x < N; ++x) TEST_ASSERT_EQUAL_HEX16(px[y * N + x], band[(N - 1 - x) * 16 + (y - y0)]);
    }
}

// The mockup's seg, step by step through fillRect.
static void refSeg(ui::Canvas& cv, int x0, int y0, int x1, int y1, int b, uint32_t rgb, float a) {
    const int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, h = b >> 1;
    int e = dx + dy;
    for (int n = 0; n < 4000; ++n) {
        cv.fillRect(x0 - h, y0 - h, b, b, rgb, a);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static void test_seg_matches_the_mockup_in_bands() {
    // Random lines, many reaching far off the canvas, drawn whole and in clipped bands: the closed-form steps must
    // land every block where the mockup's loop does.
    static uint16_t a[N * N], b[N * N];
    uint32_t seed = 12345;
    auto rnd = [&](int lo, int hi) { seed = seed * 1103515245u + 12345u; return lo + static_cast<int>((seed >> 8) % (hi - lo + 1)); };
    for (int t = 0; t < 3000; ++t) {
        const int x0 = rnd(-60, 90), y0 = rnd(-60, 90), x1 = rnd(-60, 90), y1 = rnd(-60, 90), w = rnd(1, 6);
        const float al = t % 3 ? 1.f : 0.3f;
        memset(a, 0, sizeof(a));
        memset(b, 0, sizeof(b));
        ui::Canvas ca(a, N, N), cb(b, N, N);
        refSeg(ca, x0, y0, x1, y1, w, 0xFFFFFF, al);
        const int band = rnd(0, 24);
        cb.clip(0, band, N, 7);
        ca.clip(0, band, N, 7);
        memset(a, 0, sizeof(a));
        refSeg(ca, x0, y0, x1, y1, w, 0xFFFFFF, al);
        cb.seg(x0, y0, x1, y1, w, 0xFFFFFF, al);
        TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
    }
}

static void test_roundi_matches_lroundf() {
    // Every rounding the canvas does goes through roundi: it must give lroundf's answer for every float, halfway cases
    // (away from zero), values a hair either side of them, both signs, near and past 2^23 (where it hands over to
    // lroundf), and infinities.
    const float halves[] = {0.5f, 1.5f, 2.5f, -0.5f, -1.5f, -2.5f, 254.5f, -254.5f, 8388607.5f, -8388607.5f, 4194303.5f};
    for (float h : halves) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(lroundf(h)), ui::roundi(h));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(lroundf(nextafterf(h, 0))), ui::roundi(nextafterf(h, 0)));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(lroundf(nextafterf(h, h * 4))), ui::roundi(nextafterf(h, h * 4)));
    }
    const float edges[] = {0.f, -0.f, 0.49999997f, -0.49999997f, 8388608.f, -8388608.f, 8388609.f, 16777217.f, 1e9f,
                           -1e9f, 3e-39f, -3e-39f};
    for (float v : edges) TEST_ASSERT_EQUAL_INT(static_cast<int>(lroundf(v)), ui::roundi(v));
    // Past what an int holds: lroundf's own answer, whatever the platform makes of it.
    const float huge[] = {3e9f, -3e9f, 1e20f, -1e20f};
    for (float h : huge) {
        volatile float v = h;   // not folded at compile time
        TEST_ASSERT_EQUAL_INT(static_cast<int>(lroundf(v)), ui::roundi(v));
    }
    uint32_t seed = 99;
    for (int t = 0; t < 200000; ++t) {
        seed = seed * 1103515245u + 12345u;
        const uint32_t bits = seed;
        float v;
        memcpy(&v, &bits, 4);
        if (!(fabsf(v) < 2e9f)) continue;   // outside int: lroundf's answer doesn't fit what callers keep
        TEST_ASSERT_EQUAL_INT(static_cast<int>(lroundf(v)), ui::roundi(v));
        const float w = static_cast<float>(static_cast<int>(seed % 20001) - 10000) / 4.f;   // quarters: many halves
        TEST_ASSERT_EQUAL_INT(static_cast<int>(lroundf(w)), ui::roundi(w));
    }
}

namespace {
// The canvases a seg or a fill is checked on: a whole screen; a band laid out a quarter turn right (the dock's, rows
// along memory) or left; and a band rotated and byte-swapped (the handheld's panelBand).
ui::Canvas testCanvas(uint16_t* px, int kind, int y0band) {
    switch (kind) {
        case 1: return ui::Canvas::strided(px, (N - 1) * 16 - y0band, -16, 1, 0, y0band, N, y0band + 16);
        case 2: return ui::Canvas::strided(px, y0band + 15, 16, -1, 0, y0band, N, y0band + 16);
        case 3: return ui::Canvas::panelBand(px, N, y0band, 16);
        default: return ui::Canvas(px, N, N);
    }
}
}  // namespace

static void test_seg_matches_each_block() {
    // seg works out its steps directly, writes an opaque line's leading edges straight, and counts how many of a faded
    // line's blocks cover each pixel to blend it that many times: every pixel must be left exactly as the mockup's
    // block-by-block fills leave it, over any background, in any color and alpha (opaque too), any width (those too
    // wide to count are blended block by block), translated and clipped, on every kind of canvas, from either end.
    static uint16_t a[N * N], b[N * N], under[N * N];
    uint32_t seed = 2026;
    auto rnd = [&](int lo, int hi) { seed = seed * 1103515245u + 12345u; return lo + static_cast<int>((seed >> 8) % (hi - lo + 1)); };
    for (int t = 0; t < 8000; ++t) {
        const int x0 = rnd(-40, 70), y0 = rnd(-40, 70), w = rnd(-1, 20);
        int x1 = t % 4 ? rnd(-40, 70) : x0 + rnd(-3, 3), y1 = t % 5 ? rnd(-40, 70) : y0 + rnd(-3, 3);   // short ones too
        if (t % 11 == 0) {   // heading far off (over 250000 px, worked out in 64 bits): the blocks near this end show
            x1 = x0 + (t % 2 ? 1 : -1) * rnd(250001, 900000);
            y1 = y0 + rnd(-300000, 300000);
            if (t % 3 == 0) { const int k = x1; x1 = x0 + (y1 - y0); y1 = y0 + (k - x0); }   // steep
        }
        int sx0 = x0, sy0 = y0;
        if (t % 11 == 5) {   // starting 2000-4000 px off, crossing the canvas thousands of steps in, running on past
            // 250000 px, nearly level or nearly diagonal: the first step drawn is far along a line just over the 32-bit
            // limit, where its first numerator (2 minor x the step) would not fit 32 bits
            const int off = rnd(2000, 4000), len = rnd(250001, 900000), dir = t % 2 ? 1 : -1;
            const int minor = t % 4 < 2 ? rnd(-len / 200, len / 200) : (t % 8 < 4 ? 1 : -1) * (len - rnd(1, len / 50));
            const int back = static_cast<int>(static_cast<int64_t>(off) * minor / len);   // the minor coordinate off steps in
            if (t % 3 == 0) {   // steep
                sy0 = y0 - dir * off;
                sx0 = x0 - back;
                x1 = sx0 + minor;
                y1 = sy0 + dir * len;
            } else {
                sx0 = x0 - dir * off;
                sy0 = y0 - back;
                x1 = sx0 + dir * len;
                y1 = sy0 + minor;
            }
        }
        const uint32_t rgb = static_cast<uint32_t>(rnd(0, 0xFFFFFF));
        const float al = t % 3 == 0 ? 1.f : rnd(0, 256) / 255.f, canvasAlpha = t % 7 ? 1.f : rnd(30, 100) / 100.f;
        for (int i = 0; i < N * N; ++i) under[i] = static_cast<uint16_t>(rnd(0, 0xFFFF));
        const int kind = t % 4, y0band = rnd(0, 16), ox = rnd(-5, 5), oy = rnd(-5, 5);
        memcpy(a, under, sizeof(a));
        memcpy(b, under, sizeof(b));
        ui::Canvas ca = testCanvas(a, kind, y0band), cb = testCanvas(b, kind, y0band);
        const int cx = rnd(-5, N), cy = rnd(-5, N), cw = rnd(0, N + 10), ch = rnd(0, N + 10);
        ui::Canvas* both[2] = {&ca, &cb};
        for (ui::Canvas* cv : both) {
            cv->ox = ox;
            cv->oy = oy;
            cv->alpha = canvasAlpha;
            if (t % 6) cv->clip(cx, cy, cw, ch);
        }
        refSeg(ca, sx0, sy0, x1, y1, w, rgb, al);
        cb.seg(sx0, sy0, x1, y1, w, rgb, al);
        char msg[96];
        snprintf(msg, sizeof(msg), "t %d: (%d,%d)-(%d,%d) width %d alpha %.3f, canvas %d", t, sx0, sy0, x1, y1, w, al, kind);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, sizeof(a), msg);
    }
}

static void test_faded_fills_blend_each_pixel_once() {
    // A faded rectangle or pixel shape blends each pixel it covers once, exactly as blend() would with the same alpha,
    // whichever way the canvas lies in memory. The shape's pixels come from drawing it opaque.
    static uint16_t a[N * N], b[N * N], mask[N * N];
    uint32_t seed = 4242;
    auto rnd = [&](int lo, int hi) { seed = seed * 1103515245u + 12345u; return lo + static_cast<int>((seed >> 8) % (hi - lo + 1)); };
    for (int t = 0; t < 6000; ++t) {
        const int kind = t % 4, y0band = rnd(0, 16), ox = rnd(-4, 4), oy = rnd(-4, 4);
        const uint32_t rgb = static_cast<uint32_t>(rnd(0, 0xFFFFFF));
        const float al = rnd(1, 254) / 255.f, canvasAlpha = t % 3 ? 1.f : rnd(30, 100) / 100.f;
        const int cx = rnd(-5, N), cy = rnd(-5, N), cw = rnd(0, N + 10), ch = rnd(0, N + 10);
        for (int i = 0; i < N * N; ++i) a[i] = b[i] = static_cast<uint16_t>(rnd(0, 0xFFFF));
        memset(mask, 0, sizeof(mask));
        ui::Canvas ca = testCanvas(a, kind, y0band), cb = testCanvas(b, kind, y0band), cm = testCanvas(mask, kind, y0band);
        ui::Canvas* all[3] = {&ca, &cb, &cm};
        for (ui::Canvas* cv : all) {
            cv->ox = ox;
            cv->oy = oy;
            if (t % 5) cv->clip(cx, cy, cw, ch);
        }
        ca.alpha = cb.alpha = canvasAlpha;
        const bool shape = t % 2;
        const float x = rnd(-80, 160) / 4.f, y = rnd(-80, 160) / 4.f, w = rnd(-8, 160) / 4.f, h = rnd(-8, 160) / 4.f;
        const int sx = rnd(-10, 30), sy = rnd(-10, 30), sw = rnd(0, 40), sh = rnd(0, 40), block = rnd(1, 4);
        const uint8_t steps[3] = {static_cast<uint8_t>(rnd(0, 3)), 1, static_cast<uint8_t>(rnd(0, 2))};
        const int nSteps = rnd(0, 3);
        const bool bottom = rnd(0, 1);
        if (shape) {
            cb.fillPixelShape(sx, sy, sw, sh, block, steps, nSteps, rgb, al, bottom);
            cm.fillPixelShape(sx, sy, sw, sh, block, steps, nSteps, 0xFFFFFF, 1, bottom);
        } else {
            cb.fillRect(x, y, w, h, rgb, al);
            cm.fillRect(x, y, w, h, 0xFFFFFF);
        }
        // The reference: every pixel the opaque fill reached (device coordinates), blended once with blend().
        const int a255 = static_cast<int>(lroundf(255 * al * canvasAlpha));
        ui::Canvas ref = testCanvas(a, kind, y0band);
        for (int py = 0; py < N; ++py)
            for (int px = 0; px < N; ++px) {
                if (kind && (py < y0band || py >= y0band + 16)) continue;
                // where device pixel (px, py) is in the buffer, for each kind of canvas (testCanvas)
                const int idx = kind == 0 ? py * N + px : kind == 1 ? (N - 1 - px) * 16 + (py - y0band) : px * 16 + (y0band + 15 - py);
                if (mask[idx]) ref.blend(px, py, rgb, a255);
            }
        char msg[64];
        snprintf(msg, sizeof(msg), "t %d: %s, canvas %d, a255 %d", t, shape ? "shape" : "rect", kind, a255);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, sizeof(a), msg);
    }
}

// ---------- recording (the dock draws a frame recorded once, then played band by band) ----------

namespace {
constexpr int RW = 72, RH = 144;   // a small screen, in bands of 8, 16 or 24 rows
uint16_t whole[RW * RH], banded[RW * RH], bandBuf[RW * 24];
ui::Recording rec;

struct Rng {
    uint32_t s;
    int operator()(int lo, int hi) { s = s * 1103515245u + 12345u; return lo + static_cast<int>((s >> 8) % (hi - lo + 1)); }
    float f(float lo, float hi) { return lo + (hi - lo) * static_cast<float>((*this)(0, 10000)) / 10000.f; }
    uint32_t rgb() { return static_cast<uint32_t>((*this)(0, 0xFFFFFF)); }
};

const char* const kArt[5] = {"K..K.K", ".KKK..", "K.K.KK", "..K...", "KK..KK"};
const uint8_t kSpriteIdx[5 * 4] = {0, 1, 2, 3, 3, 0, 2, 1, 0, 0, 1, 2, 2, 1, 0, 3, 1, 1, 1, 0};
const uint16_t kPal[4] = {0x0000, 0xF800, 0x07E0, 0x001F};
// A cell grid (the dock map's basemap): classes 0-3, and 4, past a 4-color palette (left alone).
const uint8_t kCellGrid[6 * 5] = {0, 1, 1, 2, 3, 0, 4, 0, 1, 1, 2, 2, 3, 3, 0, 0, 4, 1, 2, 2, 2, 1, 0, 0, 1, 3, 3, 4, 0, 2};
const char* const kTexts[] = {"Belmont", "To 95th/Dan Ryan", "Due", "3 min", "Clark \xE2\x80\xA6", "12:05", "", " "};

// Every drawing call, with arguments reaching past the screen's edges, faded and not, under random clips,
// translations and alphas. Deterministic for a seed.
void scene(ui::Canvas& cv, uint32_t seed, int calls) {
    Rng r{seed};
    cv.clear(r.rgb());
    for (int k = 0; k < calls; ++k) {
        const float x = r.f(-30, RW + 30), y = r.f(-30, RH + 30), al = r(0, 3) ? 1.f : r.f(0, 1);
        const uint32_t rgb = r.rgb();
        switch (r(0, 22)) {
            case 0: cv.fillRect(x, y, r.f(-4, 50), r.f(-4, 50), rgb, al); break;
            case 1: {
                const uint8_t steps[3] = {static_cast<uint8_t>(r(0, 3)), 1, static_cast<uint8_t>(r(0, 2))};
                cv.fillPixelShape(static_cast<int>(x), static_cast<int>(y), r(0, 60), r(0, 60), r(1, 4), steps, r(0, 3), rgb, al, r(0, 1));
                break;
            }
            case 2: case 3: {
                ui::PlateStyle ps = r(0, 1) ? ui::smallPlate() : ui::PlateStyle();
                ps.fill = r.rgb(); ps.fill2 = r(0, 1) ? ps.fill : r.rgb(); ps.rim = r.rgb(); ps.rimAlpha = r.f(0, 1);
                ps.rimW = r(0, 3); ps.edge = r(-2, 6); ps.highlight = r(0, 1);
                cv.plate(static_cast<int>(x), static_cast<int>(y), r(-2, 70), r(-2, 70), ps);
                break;
            }
            case 4: cv.chip(kTexts[r(0, 7)], static_cast<int>(x), static_cast<int>(y), rgb, r.rgb()); break;
            case 5: case 6:
                cv.text(ui::Canvas::font(r(0, 1) ? 900 : 800, r(11, 40)), kTexts[r(0, 7)], x, y, rgb,
                        static_cast<ui::Align>(r(0, 2)), static_cast<ui::Base>(r(0, 2)), al);
                break;
            case 7:
                cv.textOutlined(ui::Canvas::font(900, r(11, 34)), kTexts[r(0, 7)], x, y, rgb, r.rgb(), r(-1, 3),
                                static_cast<ui::Align>(r(0, 2)), static_cast<ui::Base>(r(0, 2)));
                break;
            case 8: cv.pixelText(r(0, 1) ? "12:05" : "Due", x, static_cast<int>(y), r(1, 5), rgb, static_cast<ui::Align>(r(0, 2)), r(0, 2)); break;
            case 9: cv.ink(kArt, 5, static_cast<int>(x), static_cast<int>(y), r(1, 4), rgb, al); break;
            case 10: cv.sprite(kSpriteIdx, 5, 4, r(-2, 2), r(-2, 2), static_cast<int>(x), static_cast<int>(y), kPal, r(1, 4)); break;
            case 11: cv.fillCircle(x, y, r.f(0, 30), rgb, al); break;
            case 12: cv.line(x, y, r.f(-30, RW + 30), r.f(-30, RH + 30), r.f(0.5f, 8), rgb, al); break;
            case 13: cv.arc(x, y, r.f(0, 40), r.f(-7, 7), r.f(-7, 7), r.f(0.5f, 9), rgb, al, r(0, 1)); break;
            case 14: cv.arrowCircle(x, y, r.f(2, 25), rgb, r.rgb()); break;
            case 15: cv.seg(x, y, r.f(-30, RW + 30), r.f(-30, RH + 30), r(1, 12), rgb, al); break;
            case 16: cv.dot(x, y, r(2, 12), rgb, al); break;
            case 17: cv.blend(r(-5, RW + 5), r(-5, RH + 5), rgb, r(-10, 300)); break;
            case 18: cv.clip(r(-10, RW), r(-10, RH), r(0, RW + 20), r(0, RH + 20)); break;
            case 19: cv.resetClip(); break;
            case 20: cv.alpha = r(0, 2) ? 1.f : r.f(0, 1); break;
            case 21: cv.ox = r(-12, 12); cv.oy = r(-12, 12); break;
            case 22: {   // skip -1 (none) or a class left alone: the clear's color shows there, played back too
                const uint32_t pal[4] = {r.rgb(), r.rgb(), r.rgb(), r.rgb()};
                cv.cells(kCellGrid, 6, 5, static_cast<int>(x), static_cast<int>(y), r(1, 9), pal, 4, 1, r(-1, 3));
                break;
            }
        }
    }
}

// The scene drawn whole, then recorded and played band by band (rotated a quarter turn, as on the dock, either way).
void checkScene(uint32_t seed, int calls, int rows, bool cw) {
    ui::Canvas plain(whole, RW, RH);
    scene(plain, seed, calls);
    ui::Canvas rc = rec.begin(RW, RH, rows);
    scene(rc, seed, calls);
    TEST_ASSERT_TRUE(rec.end());
    TEST_ASSERT_EQUAL_INT(RH / rows, rec.bands());
    for (int b = 0; b < rec.bands(); ++b) {
        const int y0 = b * rows;
        ui::Canvas cv = cw ? ui::Canvas::strided(bandBuf, (RW - 1) * rows - y0, -rows, 1, 0, y0, RW, y0 + rows)
                           : ui::Canvas::strided(bandBuf, y0 + rows - 1, rows, -1, 0, y0, RW, y0 + rows);
        memset(bandBuf, 0xA5, sizeof(bandBuf));   // what the last band left: the clear must cover it
        rec.play(cv, b);
        for (int y = y0; y < y0 + rows; ++y)
            for (int x = 0; x < RW; ++x)
                banded[y * RW + x] = cw ? bandBuf[(RW - 1 - x) * rows + (y - y0)] : bandBuf[x * rows + (y0 + rows - 1 - y)];
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "seed %u, %d rows, %s", static_cast<unsigned>(seed), rows, cw ? "cw" : "ccw");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(whole, banded, sizeof(whole), msg);
}
}  // namespace

static void test_recording_plays_back_exactly() {
    // Thousands of random calls of every kind: each band plays only the calls whose rows reach it, so a call whose
    // rows were worked out short would leave a band missing part of it.
    for (uint32_t seed = 1; seed <= 300; ++seed) checkScene(seed, 40, seed % 3 == 0 ? 8 : seed % 3 == 1 ? 16 : 24, seed % 2);
}

static void test_recording_band_hashes() {
    // The same frame twice: the same hashes. A change to one call: only the bands it reaches change.
    uint64_t first[ui::Recording::kBands];
    auto frame = [&](float textY) {
        ui::Canvas cv = rec.begin(RW, RH, 16);
        cv.clear(0x000000);
        cv.plate(4, 2, 60, 20, ui::PlateStyle());
        cv.text(ui::Canvas::font(900, 16), "Belmont", 6, textY, 0xFFFFFF);
        cv.fillRect(0, 100, RW, 6, 0xFF0000);
        TEST_ASSERT_TRUE(rec.end());
    };
    frame(60);
    for (int b = 0; b < rec.bands(); ++b) first[b] = rec.hash(b);
    frame(60);
    for (int b = 0; b < rec.bands(); ++b) TEST_ASSERT_TRUE(first[b] == rec.hash(b));
    frame(61);   // the text moves a row: bands 3 and 4 (rows 48-79) change, nothing else
    for (int b = 0; b < rec.bands(); ++b) TEST_ASSERT_EQUAL(b == 3 || b == 4, first[b] != rec.hash(b));
    // Other text, the same length; a color; the canvas's alpha: each changes the bands it reaches.
    auto other = [&](const char* text, uint32_t rgb, float alpha) {
        ui::Canvas cv = rec.begin(RW, RH, 16);
        cv.clear(0x000000);
        cv.plate(4, 2, 60, 20, ui::PlateStyle());
        cv.alpha = alpha;
        cv.text(ui::Canvas::font(900, 16), text, 6, 60, rgb);
        cv.alpha = 1;
        cv.fillRect(0, 100, RW, 6, 0xFF0000);
        TEST_ASSERT_TRUE(rec.end());
        for (int b = 0; b < rec.bands(); ++b) TEST_ASSERT_EQUAL(b == 3 || b == 4, first[b] != rec.hash(b));
    };
    other("Belmonx", 0xFFFFFF, 1);
    other("Belmont", 0xFFFFFE, 1);
    other("Belmont", 0xFFFFFF, 0.5f);
}

static void test_cells_skip_leaves_its_class_alone() {
    // A 4 x 3 grid of 2 px cells over a canvas of 7s: class 1 (skip) leaves the 7s, the others are their colors; the
    // same with no skip draws class 1 too. Recorded, skip is part of the call (its bands' hashes change with it).
    static uint16_t a[16 * 16];
    const uint8_t grid[12] = {0, 1, 2, 1, 1, 1, 0, 2, 2, 0, 1, 0};
    const uint32_t pal[3] = {0x000000, 0xFF0000, 0x0000FF};
    for (int skip = -1; skip <= 1; skip += 2) {
        for (uint16_t& p : a) p = 7;
        ui::Canvas cv(a, 16, 16);
        cv.cells(grid, 4, 3, 1, 2, 2, pal, 3, 1, skip);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const int i = (x - 1) / 2, j = (y - 2) / 2;
                const bool in = x >= 1 && y >= 2 && i < 4 && j < 3;
                const uint8_t v = in ? grid[j * 4 + i] : 0;
                const uint16_t want = !in || v == skip ? 7 : v == 0 ? 0x0000 : v == 1 ? 0xF800 : 0x001F;
                TEST_ASSERT_EQUAL_HEX16(want, a[y * 16 + x]);
            }
    }
    uint64_t h[2];
    for (int k = 0; k < 2; ++k) {
        ui::Canvas cv = rec.begin(RW, RH, 16);
        cv.clear(0x000000);
        cv.cells(grid, 4, 3, 1, 2, 2, pal, 3, 1, k ? 1 : -1);
        TEST_ASSERT_TRUE(rec.end());
        h[k] = rec.hash(0);
    }
    TEST_ASSERT_TRUE(h[0] != h[1]);
}

static void test_plate_is_its_shadow_then_itself() {
    // A plate draws its shadow (the shape edge rows down, a darker fill2) and then itself over it. Opaque, it draws
    // only the part of the shadow that shows: the same pixels as the whole shadow first, faded or not, clipped or not,
    // on a whole canvas or a band.
    static uint16_t a[RW * RH], b[RW * RH];
    Rng r{777};
    for (int t = 0; t < 3000; ++t) {
        ui::PlateStyle ps = r(0, 1) ? ui::smallPlate() : ui::PlateStyle();
        ps.fill = r.rgb(); ps.fill2 = r(0, 1) ? ps.fill : r.rgb(); ps.rim = r.rgb(); ps.rimAlpha = r.f(0, 1);
        ps.rimW = r(0, 3); ps.edge = r(-3, 8); ps.highlight = r(0, 1);
        const int x = r(-20, RW), y = r(-20, RH), w = r(0, 80), h = r(0, 80), clipY = r(-10, RH), clipH = r(0, RH);
        const float alpha = r(0, 2) ? 1.f : r.f(0, 1);
        const bool band = r(0, 1);
        const uint32_t under = r.rgb();
        auto canvas = [&](uint16_t* px) {
            return band ? ui::Canvas::strided(px, (RW - 1) * 24 - 48, -24, 1, 0, 48, RW, 72) : ui::Canvas(px, RW, RH);
        };
        ui::Canvas ca = canvas(a), cb = canvas(b);
        ui::Canvas* both[2] = {&ca, &cb};
        for (ui::Canvas* cv : both) {
            cv->clear(under);
            cv->clip(0, clipY, RW, clipH);
            cv->alpha = alpha;
        }
        ca.plate(x, y, w, h, ps);
        ui::PlateStyle bare = ps;
        bare.edge = 0;
        if (ps.edge) cb.fillPixelShape(x, y + ps.edge, w, h, ps.block, ps.steps, ps.nSteps, ui::scaleRgb(ps.fill2, 0.55f));
        cb.plate(x, y, w, h, bare);
        TEST_ASSERT_EQUAL_MEMORY(b, a, (band ? RW * 24 : RW * RH) * sizeof(uint16_t));
    }
}

static void test_recording_refuses_what_it_cannot_play() {
    static uint16_t img[4] = {1, 2, 3, 4};
    ui::Canvas cv = rec.begin(RW, RH, 16);
    cv.clear(0);
    cv.blitNearest(img, 2, 2, 0, 10, 10, 1, 1);   // its source could change before playback
    TEST_ASSERT_FALSE(rec.end());
    cv = rec.begin(RW, RH, 16);
    cv.fillRect(0, 0, 4, 4, 0xFFFFFF);   // no clear first: a band would show what the last band left
    cv.clear(0);
    TEST_ASSERT_FALSE(rec.end());
    cv = rec.begin(RW, RH, 16);
    cv.clear(0);
    for (int k = 0; k < ui::Recording::kOps; ++k) cv.dot(5, 5, 3, 0xFFFFFF);   // one more call than fits
    TEST_ASSERT_FALSE(rec.end());
    cv = rec.begin(RW, RH, 16);
    cv.clear(0);
    cv.fillRect(-50, -50, 10, 10, 0xFFFFFF);   // off the screen: dropped, it changes nothing
    TEST_ASSERT_TRUE(rec.end());
    cv = rec.begin(RW, 33 * 16, 16);   // more bands than it keeps hashes for
    cv.clear(0);
    TEST_ASSERT_FALSE(rec.end());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dot_knocks_off_corners);
    RUN_TEST(test_seg_blocks_along_a_line);
    RUN_TEST(test_seg_fades_like_overlapping_rects);
    RUN_TEST(test_seg_clipped);
    RUN_TEST(test_strided_matches_row_major);
    RUN_TEST(test_seg_matches_the_mockup_in_bands);
    RUN_TEST(test_roundi_matches_lroundf);
    RUN_TEST(test_seg_matches_each_block);
    RUN_TEST(test_faded_fills_blend_each_pixel_once);
    RUN_TEST(test_recording_plays_back_exactly);
    RUN_TEST(test_recording_band_hashes);
    RUN_TEST(test_plate_is_its_shadow_then_itself);
    RUN_TEST(test_recording_refuses_what_it_cannot_play);
    RUN_TEST(test_cells_skip_leaves_its_class_alone);
    return UNITY_END();
}
