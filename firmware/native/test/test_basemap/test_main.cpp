// The dock map's pixel Chicago (basemap.h): the packed map unpacks, lands known places in the right class, and keeps
// cells anchored to the map as it pans. The same places as tools/test_basemap_landmarks.mjs checks in the mockups.
#include <math.h>
#include <unity.h>

#include "basemap.h"

void setUp() {}
void tearDown() {}

// The classes within r cells (10 m each) of a point, as a bit set.
static unsigned around(float lat, float lon, int r) {
    cta::MapFrame f{lat, lon, 111.32f * cosf(lat * 3.14159265f / 180), 111.32f, 300.f, 45.f, 45.f};
    static cta::BasemapGrid g;
    TEST_ASSERT_TRUE(cta::basemapRaster(f, 0, 0, 0, 0, 90, 90, 3, false, g));
    const int i0 = (45 - g.ox) / 3, j0 = (45 - g.oy) / 3;
    unsigned seen = 0;
    for (int j = j0 - r; j <= j0 + r; ++j)
        for (int i = i0 - r; i <= i0 + r; ++i) seen |= 1u << g.cells[j * g.cols + i];
    return seen;
}

static void test_loads() { TEST_ASSERT_TRUE(cta::basemapLoad()); }

static void test_landmarks() {
    TEST_ASSERT_TRUE(around(41.890f, -87.580f, 0) & 1u << cta::kWater);       // Lake Michigan off the Loop
    TEST_ASSERT_TRUE(around(42.010f, -87.640f, 0) & 1u << cta::kWater);       // off Rogers Park
    TEST_ASSERT_TRUE(around(41.8885f, -87.6225f, 1) & 1u << cta::kWater);     // the river at Michigan Ave
    TEST_ASSERT_TRUE(around(41.8800f, -87.6190f, 0) & 1u << cta::kGreen);     // Grant Park, Butler Field
    TEST_ASSERT_TRUE(around(41.9565f, -87.6595f, 0) & 1u << cta::kGreen);     // Graceland Cemetery
    TEST_ASSERT_TRUE(around(41.9655f, -87.6370f, 3) & 1u << cta::kSand);      // Montrose Beach
    TEST_ASSERT_TRUE(around(41.8800f, -87.6300f, 0) & ~(1u << cta::kWater));  // the Loop is land
    // CTA's stations in the expressway medians (data/stops.json): Addison (Blue) in the Kennedy, 47th (Red) in the
    // Dan Ryan, Cicero (Blue) in the Eisenhower.
    TEST_ASSERT_TRUE(around(41.946604f, -87.718458f, 3) & 1u << cta::kExpressway);
    TEST_ASSERT_TRUE(around(41.810318f, -87.63094f, 3) & 1u << cta::kExpressway);
    TEST_ASSERT_TRUE(around(41.871574f, -87.745154f, 3) & 1u << cta::kExpressway);
}

static void test_cells_stay_with_the_map_as_it_pans() {
    cta::MapFrame f{41.94f, -87.655f, 82.9f, 111.32f, 107.f, 240.f, 410.f};
    cta::BasemapGrid a, b;
    TEST_ASSERT_TRUE(cta::basemapRaster(f, 0, 0, 0, 80, 480, 720, 3, false, a));
    const uint32_t v = a.version;
    TEST_ASSERT_TRUE(cta::basemapRaster(f, 9, -6, 0, 80, 480, 720, 3, false, b));   // 3 cells right, 2 up
    TEST_ASSERT_NOT_EQUAL(v, b.version);
    int same = 0, total = 0;
    for (int j = 2; j < a.rows - 4; ++j)
        for (int i = 2; i < a.cols - 5; ++i) {
            // the ground under a's cell (i, j) moves 9 px right and 6 px up: which b cell is it in?
            const int bi = (a.ox + 3 * i + 9 - b.ox) / 3, bj = (a.oy + 3 * j - 6 - b.oy) / 3;
            if (bi < 0 || bj < 0 || bi >= b.cols || bj >= b.rows) continue;
            ++total;
            same += a.cells[j * a.cols + i] == b.cells[bj * b.cols + bi];
        }
    TEST_ASSERT_TRUE(total > 30000);
    TEST_ASSERT_EQUAL_INT(total, same);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_loads);
    RUN_TEST(test_landmarks);
    RUN_TEST(test_cells_stay_with_the_map_as_it_pans);
    return UNITY_END();
}
