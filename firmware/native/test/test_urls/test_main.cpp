#include <string.h>
#include <unity.h>

#include "urls.h"

void setUp() {}
void tearDown() {}

static void test_urls() {
    char u[256];
    TEST_ASSERT_TRUE(cta::trainArrivalsUrl(u, sizeof u, "K", "41320"));
    TEST_ASSERT_NOT_NULL(strstr(u, "mapid=41320"));
    TEST_ASSERT_NOT_NULL(strstr(u, "outputType=JSON"));
    TEST_ASSERT_TRUE(cta::trainArrivalsUrl(u, sizeof u, "K", "30255"));
    TEST_ASSERT_NOT_NULL(strstr(u, "stpid=30255"));
    const char* lines[] = {"Red", "Brn"};
    TEST_ASSERT_TRUE(cta::trainPositionsUrl(u, sizeof u, "K", lines, 2));
    TEST_ASSERT_NOT_NULL(strstr(u, "ttpositions.aspx?key=K&rt=Red,Brn&outputType=JSON"));
    const char* eleven[11] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
    TEST_ASSERT_FALSE(cta::busPredictionsUrl(u, sizeof u, "K", eleven, 11));
    TEST_ASSERT_FALSE(cta::busPredictionsUrl(u, sizeof u, "K", eleven, 0));
    const char* two[] = {"1001", "1002"};
    TEST_ASSERT_TRUE(cta::busPredictionsUrl(u, sizeof u, "K", two, 2));
    TEST_ASSERT_NOT_NULL(strstr(u, "stpid=1001,1002&format=json"));
    TEST_ASSERT_TRUE(cta::busStopsUrl(u, sizeof u, "K", "22", "North Bound"));
    TEST_ASSERT_NOT_NULL(strstr(u, "rt=22&dir=North%20Bound&format=json"));
    TEST_ASSERT_TRUE(cta::alertsUrl(u, sizeof u, two, 2));
    TEST_ASSERT_NOT_NULL(strstr(u, "routeid=1001,1002"));
    TEST_ASSERT_TRUE(cta::weatherUrl(u, sizeof u, 41.87812, -87.62980));
    TEST_ASSERT_NOT_NULL(strstr(u, "latitude=41.8781&longitude=-87.6298"));
    TEST_ASSERT_FALSE(cta::weatherUrl(u, sizeof u, 91, 0));
    TEST_ASSERT_TRUE(cta::geoIpUrl(u, sizeof u));
    TEST_ASSERT_EQUAL_STRING("https://ipapi.co/json/", u);
    const char* many[] = {"Red", "Blue", "Brn", "G", "Org", "P", "Pink", "Y", "8", "9", "22", "36", "49", "50", "53", "56",
                          "60", "62", "66", "72", "74", "77", "80", "82", "151", "152"};   // a downtown handheld
    char big[512];
    TEST_ASSERT_TRUE(cta::alertsUrl(big, sizeof big, many, 26));
    TEST_ASSERT_NOT_NULL(strstr(big, "routeid=Red,Blue,Brn,G,Org,P,Pink,Y,8,9,22"));
    TEST_ASSERT_NOT_NULL(strstr(big, ",151,152"));
    TEST_ASSERT_FALSE(cta::alertsUrl(u, 100, many, 26));   // too long for the buffer: false, never cut off
    TEST_ASSERT_FALSE(cta::trainArrivalsUrl(u, 20, "K", "41320"));
    TEST_ASSERT_FALSE(cta::busPatternsUrl(u, 20, "K", "22"));
    TEST_ASSERT_FALSE(cta::weatherUrl(u, 20, 1, 1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_urls);
    return UNITY_END();
}
