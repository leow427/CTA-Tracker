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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_train);
    RUN_TEST(test_bus);
    RUN_TEST(test_alerts_and_weather);
    RUN_TEST(test_encoding);
    RUN_TEST(test_cap);
    return UNITY_END();
}
