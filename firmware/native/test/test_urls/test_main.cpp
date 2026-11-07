// URL builders (plan 1, task 9). Mirrors the app's URLTests.
#include <unity.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "urls.h"

void setUp() {}
void tearDown() {}

static char buf[512];

static void contains(const char* needle) {
    if (strstr(buf, needle) == nullptr) {
        char msg[700];
        snprintf(msg, sizeof(msg), "'%s' not in '%s'", needle, buf);
        TEST_FAIL_MESSAGE(msg);
    }
}

static void test_train() {
    TEST_ASSERT_TRUE(cta::trainArrivalsUrl(buf, sizeof(buf), "K", "41320"));
    TEST_ASSERT_EQUAL_STRING("https://lapi.transitchicago.com/api/1.0/ttarrivals.aspx?key=K&mapid=41320&outputType=JSON", buf);
    TEST_ASSERT_TRUE(cta::trainArrivalsUrl(buf, sizeof(buf), "K", "30255"));
    contains("stpid=30255");
    contains("outputType=JSON");
    TEST_ASSERT_FALSE(cta::trainArrivalsUrl(buf, sizeof(buf), "K", "12345"));  // neither a station nor a platform
    TEST_ASSERT_FALSE(cta::trainArrivalsUrl(buf, sizeof(buf), "K", ""));
    TEST_ASSERT_TRUE(cta::trainPositionsUrl(buf, sizeof(buf), "K", "Red"));
    TEST_ASSERT_EQUAL_STRING("https://lapi.transitchicago.com/api/1.0/ttpositions.aspx?key=K&rt=Red&outputType=JSON", buf);
}

static void test_bus() {
    const char* two[] = {"1001", "1002"};
    TEST_ASSERT_TRUE(cta::busPredictionsUrl(buf, sizeof(buf), "K", two, 2));
    TEST_ASSERT_EQUAL_STRING("https://www.ctabustracker.com/bustime/api/v2/getpredictions?key=K&stpid=1001,1002&format=json", buf);
    const char* eleven[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
    TEST_ASSERT_FALSE(cta::busPredictionsUrl(buf, sizeof(buf), "K", eleven, 11));
    TEST_ASSERT_TRUE(cta::busPredictionsUrl(buf, sizeof(buf), "K", eleven, 10));
    TEST_ASSERT_FALSE(cta::busPredictionsUrl(buf, sizeof(buf), "K", eleven, 0));
    const char* routes[] = {"22", "36"};
    TEST_ASSERT_TRUE(cta::busVehiclesUrl(buf, sizeof(buf), "K", routes, 2));
    TEST_ASSERT_EQUAL_STRING("https://www.ctabustracker.com/bustime/api/v2/getvehicles?key=K&rt=22,36&format=json", buf);
    TEST_ASSERT_FALSE(cta::busVehiclesUrl(buf, sizeof(buf), "K", eleven, 11));
    TEST_ASSERT_FALSE(cta::busVehiclesUrl(buf, sizeof(buf), "K", routes, 0));
    TEST_ASSERT_TRUE(cta::busPatternsUrl(buf, sizeof(buf), "K", "22"));
    TEST_ASSERT_EQUAL_STRING("https://www.ctabustracker.com/bustime/api/v2/getpatterns?key=K&rt=22&format=json", buf);
}

static void test_alerts_and_weather() {
    const char* routes[] = {"Red", "22"};
    TEST_ASSERT_TRUE(cta::alertsUrl(buf, sizeof(buf), routes, 2));
    TEST_ASSERT_EQUAL_STRING("https://www.transitchicago.com/api/1.0/alerts.aspx?outputType=JSON&activeonly=true&routeid=Red,22", buf);
    TEST_ASSERT_FALSE(cta::alertsUrl(buf, sizeof(buf), routes, 0));
    TEST_ASSERT_TRUE(cta::weatherUrl(buf, sizeof(buf), 41.87812, -87.62980));
    TEST_ASSERT_EQUAL_STRING(
        "https://api.open-meteo.com/v1/forecast?latitude=41.8781&longitude=-87.6298&current=temperature_2m,weather_code", buf);
    TEST_ASSERT_FALSE(cta::weatherUrl(buf, sizeof(buf), NAN, 0));
    TEST_ASSERT_FALSE(cta::weatherUrl(buf, sizeof(buf), 0, NAN));
    TEST_ASSERT_TRUE(cta::weatherUrl(buf, sizeof(buf), -90, -180));  // the longest coordinates still fit
    TEST_ASSERT_EQUAL_STRING(
        "https://api.open-meteo.com/v1/forecast?latitude=-90.0000&longitude=-180.0000&current=temperature_2m,weather_code",
        buf);
    TEST_ASSERT_FALSE(cta::weatherUrl(buf, sizeof(buf), 91, 0));
    TEST_ASSERT_FALSE(cta::weatherUrl(buf, sizeof(buf), 0, 181));
}

static void test_encoding() {
    // values are percent-encoded, so a stray character can't add or break a query parameter
    TEST_ASSERT_TRUE(cta::busPatternsUrl(buf, sizeof(buf), "a b&c=d", "X9"));
    contains("key=a%20b%26c%3Dd&rt=X9");
    const char* odd[] = {"10,01", "2"};
    TEST_ASSERT_TRUE(cta::busPredictionsUrl(buf, sizeof(buf), "K", odd, 2));
    contains("stpid=10%2C01,2&");  // only the separators are bare commas
    TEST_ASSERT_TRUE(cta::trainPositionsUrl(buf, sizeof(buf), "K", "\xC3\xA9"));
    contains("rt=%C3%A9&");
}

static void test_cap() {
    const char* two[] = {"1001", "1002"};
    char small[20];
    TEST_ASSERT_FALSE(cta::trainArrivalsUrl(small, 20, "K", "41320"));
    TEST_ASSERT_FALSE(cta::trainPositionsUrl(small, 20, "K", "Red"));
    TEST_ASSERT_FALSE(cta::busPredictionsUrl(small, 20, "K", two, 2));
    TEST_ASSERT_FALSE(cta::busVehiclesUrl(small, 20, "K", two, 2));
    TEST_ASSERT_FALSE(cta::busPatternsUrl(small, 20, "K", "22"));
    TEST_ASSERT_FALSE(cta::alertsUrl(small, 20, two, 2));
    TEST_ASSERT_FALSE(cta::weatherUrl(small, 20, 41.9, -87.6));
    TEST_ASSERT_FALSE(cta::trainArrivalsUrl(nullptr, 0, "K", "41320"));
    // exactly fitting (length + NUL) works, one byte less doesn't, and nothing is written past cap
    TEST_ASSERT_TRUE(cta::busPatternsUrl(buf, sizeof(buf), "K", "22"));
    const size_t need = strlen(buf) + 1;
    char exact[200];
    memset(exact, '#', sizeof(exact));
    TEST_ASSERT_TRUE(cta::busPatternsUrl(exact, need, "K", "22"));
    TEST_ASSERT_EQUAL_STRING(buf, exact);
    memset(exact, '#', sizeof(exact));
    TEST_ASSERT_FALSE(cta::busPatternsUrl(exact, need - 1, "K", "22"));
    TEST_ASSERT_EQUAL_CHAR('#', exact[need - 1]);
}

// Every builder at every cap from 0 to need + 1: true only when it fits with its NUL, nothing written at or past
// cap, and "" left on failure, so a half-written URL holding the key never reads as a string.
static void sweep(const char* name, bool (*build)(char*, size_t)) {
    char full[512];
    TEST_ASSERT_TRUE_MESSAGE(build(full, sizeof(full)), name);
    const size_t need = strlen(full) + 1;
    char b[600];
    for (size_t cap = 0; cap <= need + 1; ++cap) {
        memset(b, '#', sizeof(b));
        const bool ok = build(b, cap);
        TEST_ASSERT_EQUAL_MESSAGE(cap >= need, ok, name);
        for (size_t i = cap; i < sizeof(b); ++i) TEST_ASSERT_EQUAL_CHAR_MESSAGE('#', b[i], name);
        if (ok) TEST_ASSERT_EQUAL_STRING_MESSAGE(full, b, name);
        if (!ok && cap > 0) TEST_ASSERT_EQUAL_CHAR_MESSAGE('\0', b[0], name);
    }
}

static void test_cap_sweep() {
    static const char* two[] = {"1001", "1002"};
    sweep("arrivals", [](char* b, size_t c) { return cta::trainArrivalsUrl(b, c, "K", "41320"); });
    sweep("positions", [](char* b, size_t c) { return cta::trainPositionsUrl(b, c, "K", "Red"); });
    sweep("predictions", [](char* b, size_t c) { return cta::busPredictionsUrl(b, c, "K", two, 2); });
    sweep("vehicles", [](char* b, size_t c) { return cta::busVehiclesUrl(b, c, "K", two, 2); });
    sweep("patterns", [](char* b, size_t c) { return cta::busPatternsUrl(b, c, "a b", "X"); });  // %XX at the cap
    sweep("alerts", [](char* b, size_t c) { return cta::alertsUrl(b, c, two, 2); });
    sweep("weather", [](char* b, size_t c) { return cta::weatherUrl(b, c, -90, -180); });
    // invalid arguments leave "" too
    char b[200];
    memset(b, '#', sizeof(b));
    TEST_ASSERT_FALSE(cta::trainArrivalsUrl(b, sizeof(b), "K", "12345"));
    TEST_ASSERT_EQUAL_STRING("", b);
    memset(b, '#', sizeof(b));
    TEST_ASSERT_FALSE(cta::weatherUrl(b, sizeof(b), 0, NAN));
    TEST_ASSERT_EQUAL_STRING("", b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_train);
    RUN_TEST(test_bus);
    RUN_TEST(test_alerts_and_weather);
    RUN_TEST(test_encoding);
    RUN_TEST(test_cap);
    RUN_TEST(test_cap_sweep);
    return UNITY_END();
}
