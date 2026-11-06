#include <unity.h>

#include "line_colors.h"
#include "local_time.h"
#include "scheduler.h"
#include "text.h"

void setUp() {}
void tearDown() {}

static void test_line_colors() {
    TEST_ASSERT_EQUAL_HEX32(0xC60C30, cta::lineColor("Red"));
    TEST_ASSERT_EQUAL_HEX32(0xC60C30, cta::lineColor("red"));   // ttpositions uses lowercase
    TEST_ASSERT_EQUAL_HEX32(0x00A1DE, cta::lineColor("Blue"));
    TEST_ASSERT_EQUAL_HEX32(0x62361B, cta::lineColor("Brn"));
    TEST_ASSERT_EQUAL_HEX32(0x009B3A, cta::lineColor("G"));
    TEST_ASSERT_EQUAL_HEX32(0xF9461C, cta::lineColor("Org"));
    TEST_ASSERT_EQUAL_HEX32(0xE27EA6, cta::lineColor("Pink"));
    TEST_ASSERT_EQUAL_HEX32(0x522398, cta::lineColor("P"));
    TEST_ASSERT_EQUAL_HEX32(0xF9E300, cta::lineColor("Y"));
    TEST_ASSERT_EQUAL_HEX32(0x565A5C, cta::lineColor("22"));
    TEST_ASSERT_EQUAL_HEX32(0x2F7BFF, cta::accent("22"));
    TEST_ASSERT_EQUAL_HEX32(0xC60C30, cta::accent("Red"));
}

static void test_card_shades_read() {
    // Every card's text reads at 4.5:1 on its fill's lighter top, like the app's contrast test.
    for (const auto& l : cta::kTrainLineInfo) {
        uint32_t top = cta::cardTop(l.rgb), text = cta::cardText(l.rgb);
        TEST_ASSERT_TRUE_MESSAGE(cta::contrast(text, top) >= 4.5, l.route);
        TEST_ASSERT_TRUE_MESSAGE(cta::contrast(text, cta::card(l.rgb)) >= 4.5, l.route);
        TEST_ASSERT_TRUE_MESSAGE(cta::luminance(cta::card(l.rgb)) < cta::luminance(l.rgb), l.route);
    }
    TEST_ASSERT_TRUE(cta::contrast(cta::cardText(cta::kBusAccent), cta::cardTop(cta::kBusAccent)) >= 4.5);
    TEST_ASSERT_TRUE(cta::contrast(cta::onNavy(0x522398), cta::kNavy) >= 4.5);
}

static void test_cta_time() {
    int64_t a, b;
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T08:00:00", a));
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T08:03:00", b));
    TEST_ASSERT_EQUAL_INT64(180, b - a);
    TEST_ASSERT_TRUE(cta::parseCtaTime("20261105 08:00", a));
    TEST_ASSERT_TRUE(cta::parseCtaTime("20261105 08:04:30", b));
    TEST_ASSERT_EQUAL_INT64(270, b - a);
    cta::parseCtaTime("2026-11-05T23:58:30", a);
    cta::parseCtaTime("2026-11-06T00:03:30", b);
    TEST_ASSERT_EQUAL_INT64(300, b - a);
    cta::parseCtaTime("2026-12-31T23:59:00", a);
    cta::parseCtaTime("2027-01-01T00:01:00", b);
    TEST_ASSERT_EQUAL_INT64(120, b - a);
    TEST_ASSERT_TRUE(cta::parseCtaTime("1970-01-01T00:00:00", a));
    TEST_ASSERT_EQUAL_INT64(0, a);
    TEST_ASSERT_FALSE(cta::parseCtaTime("garbage", a));
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-02-30T08:00:00", a));
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08:00:00Z", a));
    TEST_ASSERT_FALSE(cta::parseCtaTime(nullptr, a));
}

static void test_night_hours() {
    TEST_ASSERT_TRUE(cta::inNightHours(60, 60, 300));
    TEST_ASSERT_FALSE(cta::inNightHours(300, 60, 300));
    TEST_ASSERT_FALSE(cta::inNightHours(59, 60, 300));
    TEST_ASSERT_TRUE(cta::inNightHours(23 * 60 + 30, 23 * 60, 6 * 60));
    TEST_ASSERT_TRUE(cta::inNightHours(30, 23 * 60, 6 * 60));
    TEST_ASSERT_FALSE(cta::inNightHours(7 * 60, 23 * 60, 6 * 60));
    TEST_ASSERT_FALSE(cta::inNightHours(100, 120, 120));
}

static void test_truncation() {
    char out[25];
    cta::copyTruncated(out, "Harlem/Lake (via Marketplace)", 24);
    TEST_ASSERT_EQUAL_STRING("Harlem/Lake (via Marketp", out);
    cta::copyTruncated(out, "AAAAAAAAAAAAAAAAAAAAAAA\xC3\xA9", 24);   // é would straddle byte 24
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAAAAAAAAA", out);
    cta::copyTruncated(out, "AAAAAAAAAAAAAAAAAAAAAA\xC3\xA9", 24);    // é ends exactly at 24
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAAAAAAAA\xC3\xA9", out);
    cta::copyTruncated(out, nullptr, 24);
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_scheduler() {
    cta::Job j{30};
    TEST_ASSERT_TRUE(j.isDue(0, false));   // first call always due
    j.markRun(100);
    TEST_ASSERT_FALSE(j.isDue(129, false));
    TEST_ASSERT_TRUE(j.isDue(130, false));
    TEST_ASSERT_FALSE(j.isDue(130, true));   // night: every 5 min
    TEST_ASSERT_TRUE(j.isDue(400, true));
    cta::Job w{900};
    w.markRun(0);
    TEST_ASSERT_FALSE(w.isDue(300, true));   // slow mode never speeds a job up
    j.reset();
    TEST_ASSERT_TRUE(j.isDue(101, false));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_line_colors);
    RUN_TEST(test_card_shades_read);
    RUN_TEST(test_cta_time);
    RUN_TEST(test_night_hours);
    RUN_TEST(test_truncation);
    RUN_TEST(test_scheduler);
    return UNITY_END();
}
