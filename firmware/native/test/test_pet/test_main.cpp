// Trackie's sprites and renderer (plan 1, task 11).
#include <unity.h>

#include <string.h>

#include "cta_types.h"
#include "pet_render.h"
#include "pet_sprites.h"

void setUp() {}
void tearDown() {}

static void test_rgb565() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, pet::rgb565(0xFFFFFF));
    TEST_ASSERT_EQUAL_HEX16(0x2B5C, pet::rgb565(0x2D6BE6));
    TEST_ASSERT_EQUAL_HEX16(0x0000, pet::rgb565(0x000000));
    TEST_ASSERT_EQUAL_HEX16(0xF800, pet::rgb565(0xFF0000));
}

static uint16_t dst[64 * 64];

static void test_render_one_pixel_at_scale_2() {
    uint8_t idx[32 * 32] = {0};
    idx[0] = 6;
    const pet::Sprite s = {idx, 32, 32, 0, 0};
    uint16_t pal[16] = {0};
    pal[6] = 0xABCD;
    for (uint16_t& p : dst) p = 0x1234;
    pet::renderSprite(s, 0, 0, pal, 2, dst, 64, 64);
    int changed = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (dst[y * 64 + x] != 0x1234) {
                ++changed;
                TEST_ASSERT_TRUE(x < 2 && y < 2);
                TEST_ASSERT_EQUAL_HEX16(0xABCD, dst[y * 64 + x]);
            }
    TEST_ASSERT_EQUAL_INT(4, changed);
}

static void test_render_offsets_and_clips() {
    uint8_t idx[4 * 3];
    memset(idx, 1, sizeof(idx));
    uint16_t pal[2] = {0, 0x7777};
    // dy = −6 at y = 0: only rows that land inside are drawn, nothing outside the buffer
    static uint16_t guard[3 * 64 * 64];
    for (uint16_t& p : guard) p = 0x1234;
    uint16_t* mid = guard + 64 * 64;
    const pet::Sprite up = {idx, 4, 3, 0, -6};
    pet::renderSprite(up, 0, 0, pal, 4, mid, 64, 64);
    for (int i = 0; i < 64 * 64; ++i) TEST_ASSERT_EQUAL_HEX16(0x1234, guard[i]);  // above the buffer
    for (int i = 0; i < 64 * 64; ++i) TEST_ASSERT_EQUAL_HEX16(0x1234, mid[i]);    // rows −6..−4 are all above
    const pet::Sprite near = {idx, 4, 3, 0, -2};
    pet::renderSprite(near, 0, 0, pal, 4, mid, 64, 64);
    TEST_ASSERT_EQUAL_HEX16(0x7777, mid[0]);          // row 0 of the buffer is sprite row 2
    TEST_ASSERT_EQUAL_HEX16(0x7777, mid[3 * 64 + 15]);
    TEST_ASSERT_EQUAL_HEX16(0x1234, mid[4 * 64]);     // nothing below
    TEST_ASSERT_EQUAL_HEX16(0x1234, mid[16]);         // nothing right of 4 art px × 4
    // off the right and bottom edges, and negative x
    const pet::Sprite edge = {idx, 4, 3, 0, 0};
    pet::renderSprite(edge, 60, 62, pal, 4, mid, 64, 64);
    pet::renderSprite(edge, -100, -100, pal, 4, mid, 64, 64);
    for (int i = 0; i < 64 * 64; ++i) TEST_ASSERT_EQUAL_HEX16(0x1234, guard[2 * 64 * 64 + i]);  // below the buffer
    TEST_ASSERT_EQUAL_HEX16(0x7777, mid[63 * 64 + 63]);
}

static void test_pet_state_for() {
    TEST_ASSERT_EQUAL_INT((int)pet::PetState::Asleep, (int)pet::petStateFor(cta::Mood::Happy, true));
    TEST_ASSERT_EQUAL_INT((int)pet::PetState::Asleep, (int)pet::petStateFor(cta::Mood::Hyped, true));
    TEST_ASSERT_EQUAL_INT((int)pet::PetState::Sleepy, (int)pet::petStateFor(cta::Mood::Sleepy, false));
    TEST_ASSERT_EQUAL_INT((int)pet::PetState::Sad, (int)pet::petStateFor(cta::Mood::Sad, false));
    TEST_ASSERT_EQUAL_INT((int)pet::PetState::Worried, (int)pet::petStateFor(cta::Mood::Worried, false));
    TEST_ASSERT_EQUAL_INT((int)pet::PetState::Happy, (int)pet::petStateFor(cta::Mood::Happy, false));
    TEST_ASSERT_EQUAL_INT((int)pet::PetState::Hyped, (int)pet::petStateFor(cta::Mood::Hyped, false));
}

static void test_generated_sprites() {
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_NOT_NULL(pet::kPet[i].idx);
        TEST_ASSERT_EQUAL_UINT8(32, pet::kPet[i].w);
        TEST_ASSERT_EQUAL_UINT8(32, pet::kPet[i].h);
        for (int p = 0; p < 32 * 32; ++p) TEST_ASSERT_TRUE(pet::kPet[i].idx[p] < pet::kPaletteSize);
    }
    TEST_ASSERT_NULL(pet::kPetExtra[(int)pet::PetState::Sad]);  // Sad's tears are on its face
    TEST_ASSERT_NOT_NULL(pet::kPetExtra[(int)pet::PetState::Happy]);
    TEST_ASSERT_EQUAL_PTR(pet::kPetExtra[(int)pet::PetState::Sleepy], pet::kPetExtra[(int)pet::PetState::Asleep]);
    TEST_ASSERT_TRUE(pet::kOverlayUmbrella.dy < 0);  // held over the roof, above the body
    TEST_ASSERT_EQUAL_HEX16(pet::rgb565(0x2D6BE6), pet::kDefaultPalette565[pet::kPaintSlot]);
    TEST_ASSERT_EQUAL_HEX16(pet::rgb565(0x7DB4FF), pet::kDefaultPalette565[pet::kPaintLightSlot]);
    // the faces differ: each mood has its own art
    TEST_ASSERT_NOT_EQUAL(0, memcmp(pet::kPet[(int)pet::PetState::Happy].idx, pet::kPet[(int)pet::PetState::Sad].idx, 1024));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rgb565);
    RUN_TEST(test_render_one_pixel_at_scale_2);
    RUN_TEST(test_render_offsets_and_clips);
    RUN_TEST(test_pet_state_for);
    RUN_TEST(test_generated_sprites);
    return UNITY_END();
}
