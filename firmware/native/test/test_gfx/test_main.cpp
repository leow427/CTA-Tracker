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
const char* const kTexts[] = {"Belmont", "To 95th/Dan Ryan", "Due", "3 min", "Clark \xE2\x80\xA6", "12:05", "", " "};

// Every drawing call, with arguments reaching past the screen's edges, faded and not, under random clips,
// translations and alphas. Deterministic for a seed.
void scene(ui::Canvas& cv, uint32_t seed, int calls) {
    Rng r{seed};
    cv.clear(r.rgb());
    for (int k = 0; k < calls; ++k) {
        const float x = r.f(-30, RW + 30), y = r.f(-30, RH + 30), al = r(0, 3) ? 1.f : r.f(0, 1);
        const uint32_t rgb = r.rgb();
        switch (r(0, 21)) {
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
    RUN_TEST(test_recording_plays_back_exactly);
    RUN_TEST(test_recording_band_hashes);
    RUN_TEST(test_plate_is_its_shadow_then_itself);
    RUN_TEST(test_recording_refuses_what_it_cannot_play);
    return UNITY_END();
}
