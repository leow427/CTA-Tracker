#include <unity.h>

#include <vector>

#include "plate.h"

void setUp() {}
void tearDown() {}

static void test_insets_step_like_the_app() {
    pet::Shape s;   // block 4, steps 3,1,1: rows 0-3 in 12, rows 4-11 in 4, then square
    TEST_ASSERT_EQUAL_INT(12, pet::shapeInset(s, 0, 40));
    TEST_ASSERT_EQUAL_INT(12, pet::shapeInset(s, 3, 40));
    TEST_ASSERT_EQUAL_INT(4, pet::shapeInset(s, 4, 40));
    TEST_ASSERT_EQUAL_INT(4, pet::shapeInset(s, 11, 40));
    TEST_ASSERT_EQUAL_INT(0, pet::shapeInset(s, 12, 40));
    TEST_ASSERT_EQUAL_INT(0, pet::shapeInset(s, 27, 40));
    TEST_ASSERT_EQUAL_INT(4, pet::shapeInset(s, 28, 40));
    TEST_ASSERT_EQUAL_INT(12, pet::shapeInset(s, 39, 40));
    TEST_ASSERT_EQUAL_INT(12, pet::shapeInset(s, 5, 8));   // too short: the bigger inset wins
}

static void test_blend() {
    TEST_ASSERT_EQUAL_HEX32(0xFF123456, pet::blend(0xFF000000, 0x123456, 255));
    TEST_ASSERT_EQUAL_HEX32(0xFF808080, pet::blend(0xFF000000, 0xFFFFFF, 128));
    TEST_ASSERT_EQUAL_HEX32(0x80FFFFFF, pet::blend(0x00000000, 0xFFFFFF, 128));   // over nothing keeps its alpha
}

static void test_plate_corners_and_edge() {
    const int W = 60, H = 40;
    std::vector<uint32_t> buf(W * (H + 5), 0);
    pet::Canvas c{buf.data(), W, H + 5};
    pet::PlateStyle p{0x404040, 0x202020};
    pet::drawPlate(c, 0, 0, W, H, p);
    TEST_ASSERT_EQUAL_HEX32(0, buf[0]);                 // the stepped corner stays clear
    TEST_ASSERT_EQUAL_HEX32(0, buf[11]);
    TEST_ASSERT_NOT_EQUAL(0, buf[12]);
    TEST_ASSERT_NOT_EQUAL(0, buf[(H + 2) * W + 30]);   // the edge shows under the plate
    uint32_t mid = buf[20 * W + 30];
    TEST_ASSERT_EQUAL_HEX32(0xFF, mid >> 24);
    uint32_t rim = buf[20 * W + 0], inner = buf[20 * W + 5];   // mid-height: no corner step
    TEST_ASSERT_TRUE((rim & 0xFF) > (inner & 0xFF));    // the rim is lighter than the fill beside it
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_insets_step_like_the_app);
    RUN_TEST(test_blend);
    RUN_TEST(test_plate_corners_and_edge);
    return UNITY_END();
}
