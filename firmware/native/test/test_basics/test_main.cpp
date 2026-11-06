#include <unity.h>

#include <string.h>

#include "ble_ids.h"
#include "cta_config.h"
#include "cta_types.h"
#include "line_colors.h"
#include "local_time.h"
#include "../fixture.h"

void setUp() {}
void tearDown() {}

static void test_line_colors() {
    TEST_ASSERT_EQUAL_HEX32(0xC60C30, cta::lineColor("Red"));
    TEST_ASSERT_EQUAL_HEX32(0xC60C30, cta::lineColor("red"));  // ttpositions uses lowercase
    TEST_ASSERT_EQUAL_HEX32(0x00A1DE, cta::lineColor("Blue"));
    TEST_ASSERT_EQUAL_HEX32(0x62361B, cta::lineColor("Brn"));
    TEST_ASSERT_EQUAL_HEX32(0x009B3A, cta::lineColor("G"));
    TEST_ASSERT_EQUAL_HEX32(0xF9461C, cta::lineColor("Org"));
    TEST_ASSERT_EQUAL_HEX32(0xE27EA6, cta::lineColor("Pink"));
    TEST_ASSERT_EQUAL_HEX32(0x522398, cta::lineColor("P"));
    TEST_ASSERT_EQUAL_HEX32(0xF9E300, cta::lineColor("Y"));
    TEST_ASSERT_EQUAL_HEX32(0x565A5C, cta::lineColor("22"));  // every bus route
}

static void test_line_colors_edge_cases() {
    TEST_ASSERT_EQUAL_HEX32(0x522398, cta::lineColor("p"));
    TEST_ASSERT_EQUAL_HEX32(0x565A5C, cta::lineColor("Pin"));    // a prefix of Pink is not Pink
    TEST_ASSERT_EQUAL_HEX32(0x565A5C, cta::lineColor("Reds"));   // nor a longer word
    TEST_ASSERT_EQUAL_HEX32(0x565A5C, cta::lineColor(""));
    TEST_ASSERT_EQUAL_HEX32(0x565A5C, cta::lineColor(nullptr));
}

static void test_parse_cta_time() {
    int64_t a, b;
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T08:00:00", a));
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T08:03:00", b));
    TEST_ASSERT_EQUAL_INT64(180, b - a);
    TEST_ASSERT_TRUE(cta::parseCtaTime("20261105 08:00", a));
    TEST_ASSERT_TRUE(cta::parseCtaTime("20261105 08:04:30", b));
    TEST_ASSERT_EQUAL_INT64(270, b - a);
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T23:58:30", a));
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-06T00:03:30", b));
    TEST_ASSERT_EQUAL_INT64(300, b - a);  // midnight
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-12-31T23:59:00", a));
    TEST_ASSERT_TRUE(cta::parseCtaTime("2027-01-01T00:01:00", b));
    TEST_ASSERT_EQUAL_INT64(120, b - a);  // New Year
    TEST_ASSERT_FALSE(cta::parseCtaTime("garbage", a));
}

static void test_parse_cta_time_formats_agree() {
    int64_t iso, bus;
    TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T08:04:00", iso));
    TEST_ASSERT_TRUE(cta::parseCtaTime("20261105 08:04", bus));
    TEST_ASSERT_EQUAL_INT64(iso, bus);
    TEST_ASSERT_TRUE(cta::parseCtaTime("1970-01-01T00:00:00", iso));
    TEST_ASSERT_EQUAL_INT64(0, iso);
    TEST_ASSERT_TRUE(cta::parseCtaTime("2028-02-28T23:30:00", iso));  // leap day
    TEST_ASSERT_TRUE(cta::parseCtaTime("2028-03-01T00:30:00", bus));
    TEST_ASSERT_EQUAL_INT64(25 * 3600, bus - iso);
}

static void test_parse_cta_time_rejects() {
    int64_t t = 42;
    TEST_ASSERT_FALSE(cta::parseCtaTime(nullptr, t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08:00", t));      // ISO needs seconds
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05 08:00:00", t));   // wrong separator
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08:00:00Z", t));  // trailing text
    TEST_ASSERT_FALSE(cta::parseCtaTime("20261105 08:00:", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("20261105 8:00", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-13-05T08:00:00", t));   // month 13
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-31T08:00:00", t));   // November 31
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-02-29T08:00:00", t));   // not a leap year
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T24:00:00", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08:60:00", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08:00:60", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-1a-05T08:00:00", t));
    TEST_ASSERT_EQUAL_INT64(42, t);  // untouched on failure
}

static void test_night_hours() {
    TEST_ASSERT_TRUE(cta::inNightHours(60, 60, 300));    // 1:00 starts night
    TEST_ASSERT_FALSE(cta::inNightHours(300, 60, 300));  // 5:00 ends it (exclusive)
    TEST_ASSERT_FALSE(cta::inNightHours(59, 60, 300));
    TEST_ASSERT_TRUE(cta::inNightHours(23 * 60 + 30, 23 * 60, 6 * 60));  // wraps midnight
    TEST_ASSERT_TRUE(cta::inNightHours(0, 23 * 60, 6 * 60));
    TEST_ASSERT_FALSE(cta::inNightHours(6 * 60, 23 * 60, 6 * 60));
    TEST_ASSERT_FALSE(cta::inNightHours(7 * 60, 23 * 60, 6 * 60));
    TEST_ASSERT_FALSE(cta::inNightHours(100, 120, 120));  // start == end means never
    TEST_ASSERT_TRUE(cta::inNightHours(cta::kNightStartMin, cta::kNightStartMin, cta::kNightEndMin));
}

static void test_config_values() {
    TEST_ASSERT_EQUAL_INT(30, cta::kDockPollSec);
    TEST_ASSERT_EQUAL_INT(60, cta::kHandheldClosePollSec);
    TEST_ASSERT_EQUAL_INT(180, cta::kHandheldFarPollSec);
    TEST_ASSERT_EQUAL_INT(60, cta::kHandheldBusPollSec);
    TEST_ASSERT_EQUAL_INT(300, cta::kAlertsPollSec);
    TEST_ASSERT_EQUAL_INT(900, cta::kWeatherPollSec);
    TEST_ASSERT_EQUAL_INT(300, cta::kSlowPollSec);
    TEST_ASSERT_EQUAL_INT(3, cta::kHypedMaxMin);
    TEST_ASSERT_EQUAL_INT(300, cta::kStaleSec);
    TEST_ASSERT_EQUAL_INT(120, cta::kLedStaleSec);
    TEST_ASSERT_EQUAL_INT(60, cta::kNightStartMin);
    TEST_ASSERT_EQUAL_INT(300, cta::kNightEndMin);
    TEST_ASSERT_EQUAL_STRING("CST6CDT,M3.2.0,M11.1.0", cta::kTz);
    TEST_ASSERT_EQUAL_INT(512, cta::kBleMaxPayload);
    TEST_ASSERT_EQUAL_INT(10, cta::kNearbyPerTab);
    TEST_ASSERT_EQUAL_INT(3, cta::kHypedClosestStops);
    TEST_ASSERT_EQUAL_INT(15, cta::kScreenDimSec);
    TEST_ASSERT_EQUAL_INT(4, cta::kMaxArrivals);
    TEST_ASSERT_EQUAL_INT(24, cta::kNameLen);
}

static void test_ble_ids() {
    TEST_ASSERT_EQUAL_STRING("d8d4388c-c8e3-4ff0-b269-015d0979c266", cta::kServiceUuid);
    TEST_ASSERT_EQUAL_STRING("fb360799-3925-45c3-90be-c32d504bbc99", cta::kInfoUuid);
    TEST_ASSERT_EQUAL_STRING("8d0820b1-1735-4f23-b1be-17b5d867632e", cta::kWifiUuid);
    TEST_ASSERT_EQUAL_STRING("d8427173-107a-4c28-97ba-ab3b12d1500f", cta::kConfigUuid);
    TEST_ASSERT_EQUAL_STRING("3c4dd75c-a019-4171-b7f1-b8dd87eda07c", cta::kRequestUuid);
    TEST_ASSERT_EQUAL_STRING("e8b866e4-18bf-4ab4-bd7c-07b52cb2a057", cta::kNearbyUuid);
    TEST_ASSERT_EQUAL_STRING("7d05cd5a-5784-431f-b051-deb65bc0c3ee", cta::kSnapshotUuid);
}

static void test_types_fit() {
    cta::StopRecord r{};
    TEST_ASSERT_EQUAL_UINT32(0, r.ts);  // 0 = never fetched
    TEST_ASSERT_EQUAL_INT(cta::kNameLen + 1, (int)sizeof(r.name));
    TEST_ASSERT_EQUAL_INT(cta::kMaxArrivals, (int)(sizeof(r.arrivals) / sizeof(r.arrivals[0])));
    TEST_ASSERT_EQUAL_INT(0, (int)cta::Kind::Train);
    TEST_ASSERT_EQUAL_INT(1, (int)cta::Kind::Bus);
}

static void test_read_fixture() {
    const std::string f = readFixture("train_error.json");
    TEST_ASSERT_TRUE(f.find("\"errCd\":\"101\"") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_line_colors);
    RUN_TEST(test_line_colors_edge_cases);
    RUN_TEST(test_parse_cta_time);
    RUN_TEST(test_parse_cta_time_formats_agree);
    RUN_TEST(test_parse_cta_time_rejects);
    RUN_TEST(test_night_hours);
    RUN_TEST(test_config_values);
    RUN_TEST(test_ble_ids);
    RUN_TEST(test_types_fit);
    RUN_TEST(test_read_fixture);
    return UNITY_END();
}
