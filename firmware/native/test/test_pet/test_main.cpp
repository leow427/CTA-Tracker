#include <string.h>
#include <unity.h>

#include <algorithm>
#include <vector>

#include "pet_render.h"

using pet::PetState;

void setUp() {}
void tearDown() {}

static const uint32_t kFill = 0x12345678;

static void test_sprite_scale_and_transparency() {
    static const uint8_t px[4] = {5, 0, 0, 0};   // only (0,0) = slot 5
    pet::Sprite s{2, 2, px};
    std::vector<uint32_t> buf(64 * 64, kFill);
    pet::Canvas c{buf.data(), 64, 64};
    pet::drawSprite(c, s, pet::kPalette, 2, 0, 0);
    int changed = 0;
    for (uint32_t v : buf) changed += v != kFill;
    TEST_ASSERT_EQUAL_INT(4, changed);
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u | pet::kPalette[5], buf[0]);
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u | pet::kPalette[5], buf[64 + 1]);
    TEST_ASSERT_EQUAL_HEX32(kFill, buf[2]);
}

static void test_clips_at_edges() {
    std::vector<uint32_t> buf(8 * 8, kFill);
    pet::Canvas c{buf.data(), 8, 8};
    pet::drawMono(c, pet::kGlyphAlert, 0xFFFFFF, 1, -8, -8);   // mostly off the top-left
    pet::drawMono(c, pet::kGlyphAlert, 0xFFFFFF, 3, 5, 5);     // off the bottom-right
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, buf[7 * 8 + 7]);       // the alert's bottom row is solid
}

static void test_pet_colors_and_overlays() {
    std::vector<uint32_t> buf(32 * 40, 0);
    pet::Canvas c{buf.data(), 32, 40};
    const uint32_t colors[2] = {0xC60C30, 0xE08A9E};
    pet::drawPet(c, PetState::Happy, cta::Accessory::Umbrella, colors, 1, 0, 6);
    bool body = false, light = false, canopy = false;
    for (uint32_t v : buf) {
        body |= v == (0xFF000000u | 0xC60C30);
        light |= v == (0xFF000000u | 0xE08A9E);
        canopy |= v == (0xFF000000u | 0xFF5F7A);
    }
    TEST_ASSERT_TRUE(body);
    TEST_ASSERT_TRUE(light);
    TEST_ASSERT_TRUE(canopy);
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u | 0xFF5F7A, buf[0 * 32 + 15]);   // canopy top, 6 rows above the body
    std::fill(buf.begin(), buf.end(), 0);
    pet::drawPet(c, PetState::Asleep, cta::Accessory::Umbrella, colors, 1, 0, 6);
    for (int i = 0; i < 6 * 32; i++) TEST_ASSERT_EQUAL_HEX32(0, buf[i]);   // no umbrella while asleep
}

static void test_states() {
    TEST_ASSERT_EQUAL(PetState::Asleep, pet::petStateFor(cta::Mood::Happy, true));
    TEST_ASSERT_EQUAL(PetState::Hyped, pet::petStateFor(cta::Mood::Hyped, false));
    TEST_ASSERT_EQUAL(PetState::Sleepy, pet::petStateFor(cta::Mood::Sleepy, false));
    TEST_ASSERT_NULL(pet::kExtras[(int)PetState::Sad].px);
    TEST_ASSERT_NOT_NULL(pet::kExtras[(int)PetState::Happy].px);
    for (const auto& p : pet::kPets) {
        TEST_ASSERT_EQUAL_UINT8(32, p.w);
        TEST_ASSERT_EQUAL_UINT8(32, p.h);
    }
    TEST_ASSERT_EQUAL_STRING("Go now!", pet::moodWord(PetState::Hyped));
}

static void test_pixel_text() {
    TEST_ASSERT_EQUAL_INT(5 * 2, pet::textWidth("7", 2));
    TEST_ASSERT_EQUAL_INT((5 + 1 + 5) * 3, pet::textWidth("12", 3));
    TEST_ASSERT_EQUAL_INT((5 + 1 + 1 + 1 + 5 + 1 + 5) * 1, pet::textWidth("1:05", 1));
    TEST_ASSERT_EQUAL_INT(0, pet::textWidth("", 4));
    std::vector<uint32_t> buf(20 * 8, 0);
    pet::Canvas c{buf.data(), 20, 8};
    pet::drawText(c, "1", 0xFFFFFF, 1, 0, 0);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, buf[2]);   // "..#.." top row
    TEST_ASSERT_EQUAL_HEX32(0, buf[0]);
}

static void test_weather_icons() {
    TEST_ASSERT_EQUAL_PTR(&pet::kGlyphSun, &pet::weatherIcon(0));
    TEST_ASSERT_EQUAL_PTR(&pet::kGlyphCloud, &pet::weatherIcon(3));
    TEST_ASSERT_EQUAL_PTR(&pet::kGlyphCloud, &pet::weatherIcon(61));
    TEST_ASSERT_EQUAL_PTR(&pet::kGlyphSnow, &pet::weatherIcon(73));
    TEST_ASSERT_EQUAL_PTR(&pet::kGlyphSnow, &pet::weatherIcon(86));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sprite_scale_and_transparency);
    RUN_TEST(test_clips_at_edges);
    RUN_TEST(test_pet_colors_and_overlays);
    RUN_TEST(test_states);
    RUN_TEST(test_pixel_text);
    RUN_TEST(test_weather_icons);
    return UNITY_END();
}
