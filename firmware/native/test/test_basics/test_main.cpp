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
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08:0a:00", t));   // in range if read as a digit
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08:-1:00", t));   // a negative field
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-00-05T08:00:00", t));   // month 0
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-00T08:00:00", t));   // day 0
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026/11/05T08:00:00", t));   // ISO separators
    TEST_ASSERT_FALSE(cta::parseCtaTime("2026-11-05T08.00.00", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("20261105-08:00", t));        // Bus Tracker separators
    TEST_ASSERT_FALSE(cta::parseCtaTime("20261105 08-00", t));
    TEST_ASSERT_FALSE(cta::parseCtaTime("20261105 08:04-30", t));
    TEST_ASSERT_EQUAL_INT64(42, t);  // untouched on failure
}

static void test_cta_seconds_between() {
    int64_t d;
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-05T08:00:00", "2026-11-05T08:03:00", d));
    TEST_ASSERT_EQUAL_INT64(180, d);
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261105 08:00", "20261105 08:04:30", d));
    TEST_ASSERT_EQUAL_INT64(270, d);
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-05T23:58:30", "2026-11-06T00:03:30", d));
    TEST_ASSERT_EQUAL_INT64(300, d);  // midnight
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-12-31T23:59:00", "2027-01-01T00:01:00", d));
    TEST_ASSERT_EQUAL_INT64(120, d);  // New Year
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-05T08:03:00", "2026-11-05T08:00:00", d));
    TEST_ASSERT_EQUAL_INT64(-180, d);  // an arrival before the response time stays negative; callers clamp
}

static void test_cta_seconds_between_spring_forward() {
    int64_t d;
    // 2026-03-08: 2:00 CST jumps to 3:00 CDT, so 1:58 → 3:03 is 5 min, not 65.
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-03-08T01:58:00", "2026-03-08T03:03:00", d));
    TEST_ASSERT_EQUAL_INT64(300, d);
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20260308 01:59", "20260308 03:01", d));
    TEST_ASSERT_EQUAL_INT64(120, d);
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2027-03-14T01:55:00", "2027-03-14T03:05:00", d));
    TEST_ASSERT_EQUAL_INT64(600, d);  // 2027's second Sunday of March
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-03-01T01:58:00", "2026-03-01T03:03:00", d));
    TEST_ASSERT_EQUAL_INT64(3900, d);  // an ordinary Sunday: no jump
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-03-07T23:00:00", "2026-03-08T04:00:00", d));
    TEST_ASSERT_EQUAL_INT64(4 * 3600, d);  // across the whole change
}

static void test_cta_seconds_between_fall_back() {
    int64_t d;
    // 2026-11-01: 2:00 CDT falls back to 1:00 CST, so 1:00–1:59 happens twice; the nearest reading wins.
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T01:10:00", "2026-11-01T01:15:00", d));
    TEST_ASSERT_EQUAL_INT64(300, d);  // both in the repeated hour
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T01:58:00", "2026-11-01T01:03:00", d));
    TEST_ASSERT_EQUAL_INT64(300, d);  // 1:58 CDT, then 1:03 CST
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T00:58:00", "2026-11-01T01:03:00", d));
    TEST_ASSERT_EQUAL_INT64(300, d);  // into the repeated hour
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T01:58:00", "2026-11-01T02:03:00", d));
    TEST_ASSERT_EQUAL_INT64(300, d);  // out of it
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261101 00:30", "20261101 02:30", d));
    TEST_ASSERT_EQUAL_INT64(3 * 3600, d);  // across the whole change
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2027-11-07T01:58:00", "2027-11-07T01:03:00", d));
    TEST_ASSERT_EQUAL_INT64(300, d);  // 2027's first Sunday of November
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-08T01:58:00", "2026-11-08T01:03:00", d));
    TEST_ASSERT_EQUAL_INT64(-55 * 60, d);  // a week later: no repeated hour
    // Arrivals 31–55 min out must not read as negative ("Due"): the reading nearest +25 min wins.
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T01:05:00", "2026-11-01T01:45:00", d));
    TEST_ASSERT_EQUAL_INT64(40 * 60, d);
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T01:50:00", "2026-11-01T01:25:00", d));
    TEST_ASSERT_EQUAL_INT64(35 * 60, d);  // 1:50 CDT, then 1:25 CST
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T01:00:00", "2026-11-01T01:55:00", d));
    TEST_ASSERT_EQUAL_INT64(55 * 60, d);  // the far edge of the window
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("2026-11-01T01:15:00", "2026-11-01T01:11:00", d));
    TEST_ASSERT_EQUAL_INT64(-4 * 60, d);  // a prediction slightly in the past stays slightly negative
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261101 01:30", "20261101 01:25", d));
    TEST_ASSERT_EQUAL_INT64(55 * 60, d);  // a tie (−5 or +55 min) goes to the later reading
}

static void test_cta_seconds_between_boundaries() {
    int64_t d;
    // Bus Tracker times are whole minutes, so the exact change times are common.
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20260308 01:58", "20260308 03:00", d));
    TEST_ASSERT_EQUAL_INT64(120, d);  // 3:00 is already daylight time
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20260308 01:59", "20260308 01:59", d));
    TEST_ASSERT_EQUAL_INT64(0, d);
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261101 00:59", "20261101 01:00", d));
    TEST_ASSERT_EQUAL_INT64(60, d);  // into the repeated hour at its first minute
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261101 01:29", "20261101 02:00", d));
    TEST_ASSERT_EQUAL_INT64(1860, d);  // 2:00 is already standard time
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261101 02:00", "20261101 02:05", d));
    TEST_ASSERT_EQUAL_INT64(300, d);
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261101 01:59", "20261101 01:00", d));
    TEST_ASSERT_EQUAL_INT64(60, d);  // 1:00 has a standard reading too: 1:59 CDT, then 1:00 CST
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20261101 00:30", "20261101 02:00", d));
    TEST_ASSERT_EQUAL_INT64(150 * 60, d);  // 2:00 has only its standard reading
    TEST_ASSERT_TRUE(cta::ctaSecondsBetween("20260308 01:58", "20260308 02:05", d));
    TEST_ASSERT_EQUAL_INT64(420, d);  // the skipped spring hour reads as standard time
}

static void test_cta_seconds_between_rejects() {
    int64_t d = 42;
    TEST_ASSERT_FALSE(cta::ctaSecondsBetween("garbage", "2026-11-05T08:00:00", d));
    TEST_ASSERT_FALSE(cta::ctaSecondsBetween("2026-11-05T08:00:00", "2026-02-30T08:00:00", d));
    TEST_ASSERT_FALSE(cta::ctaSecondsBetween(nullptr, nullptr, d));
    TEST_ASSERT_EQUAL_INT64(42, d);  // untouched on failure
}

static void test_http_date() {
    int64_t t = 0;
    TEST_ASSERT_TRUE(cta::parseHttpDate("Sun, 06 Nov 1994 08:49:37 GMT", t));   // RFC 9110's example
    TEST_ASSERT_EQUAL_INT64(784111777, t);
    TEST_ASSERT_TRUE(cta::parseHttpDate("Mon, 09 Nov 2026 17:28:22 GMT", t));   // the train API's, 2026-11-09
    TEST_ASSERT_EQUAL_INT64(1794245302, t);
    TEST_ASSERT_TRUE(cta::parseHttpDate("Thu, 29 Feb 2024 00:00:00 GMT", t));
    TEST_ASSERT_EQUAL_INT64(1709164800, t);
    const char* const bad[] = {"", "Sun, 06 Nov 1994 08:49:37 UTC", "Sun, 6 Nov 1994 08:49:37 GMT",
                               "Sun, 06 Xyz 1994 08:49:37 GMT", "Fri, 29 Feb 2023 00:00:00 GMT",
                               "Sun, 06 Nov 1994 24:00:00 GMT", "Sunday, 06-Nov-94 08:49:37 GMT",
                               "Sun, 06 Nov 1994 08:49:37 GMT "};
    for (const char* b : bad) {
        t = 7;
        TEST_ASSERT_FALSE_MESSAGE(cta::parseHttpDate(b, t), b);
        TEST_ASSERT_EQUAL_INT64(7, t);
    }
    TEST_ASSERT_FALSE(cta::parseHttpDate(nullptr, t));
}

static void test_night_hours() {
    TEST_ASSERT_TRUE(cta::inNightHours(60, 60, 300));    // 1:00 starts night
    TEST_ASSERT_FALSE(cta::inNightHours(300, 60, 300));  // 5:00 ends it (exclusive)
    TEST_ASSERT_FALSE(cta::inNightHours(59, 60, 300));
    TEST_ASSERT_TRUE(cta::inNightHours(23 * 60 + 30, 23 * 60, 6 * 60));  // wraps midnight
    TEST_ASSERT_TRUE(cta::inNightHours(0, 23 * 60, 6 * 60));
    TEST_ASSERT_TRUE(cta::inNightHours(23 * 60, 23 * 60, 6 * 60));  // wrapped start is inclusive
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
    TEST_ASSERT_EQUAL_INT(5, cta::kNearbyPerTab);
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
    RUN_TEST(test_cta_seconds_between);
    RUN_TEST(test_cta_seconds_between_spring_forward);
    RUN_TEST(test_cta_seconds_between_fall_back);
    RUN_TEST(test_cta_seconds_between_boundaries);
    RUN_TEST(test_cta_seconds_between_rejects);
    RUN_TEST(test_http_date);
    RUN_TEST(test_night_hours);
    RUN_TEST(test_config_values);
    RUN_TEST(test_ble_ids);
    RUN_TEST(test_types_fit);
    RUN_TEST(test_read_fixture);
    return UNITY_END();
}
