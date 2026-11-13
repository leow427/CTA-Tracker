// The canvas calls added for the dock's map (gfx2d seg and dot), against the mockup's definitions.
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dot_knocks_off_corners);
    RUN_TEST(test_seg_blocks_along_a_line);
    RUN_TEST(test_seg_fades_like_overlapping_rects);
    RUN_TEST(test_seg_clipped);
    RUN_TEST(test_strided_matches_row_major);
    RUN_TEST(test_seg_matches_the_mockup_in_bands);
    return UNITY_END();
}
