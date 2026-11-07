// Customer Alerts routes, Open-Meteo weather, and the accessory rule (plan 1, task 6).
#include <unity.h>

#include <string.h>

#include <initializer_list>
#include <string>

#include "alerts_weather.h"
#include "cta_types.h"
#include "json_util.h"
#include "../fixture.h"

void setUp() {}
void tearDown() {}

static int routesOf(const std::string& json, char routes[][8], size_t max) {
    return cta::parseAlertRoutes(json.c_str(), json.size(), routes, max);
}

static std::string alerts(const std::string& alertValue) {
    return "{\"CTAAlerts\":{\"TimeStamp\":\"2026-11-05T08:00:00\",\"ErrorCode\":\"0\",\"ErrorMessage\":null,\"Alert\":" +
           alertValue + "}}";
}

static std::string alert(const std::string& serviceValue) {
    return "{\"AlertId\":\"1\",\"Headline\":\"h\",\"FullDescription\":{\"#cdata-section\":\"<p>long text</p>\"},"
           "\"ImpactedService\":{\"Service\":" + serviceValue + "}}";
}

static std::string svc(const char* type, const char* id) {
    return std::string("{\"ServiceType\":\"") + type + "\",\"ServiceName\":\"n\",\"ServiceId\":\"" + id + "\"}";
}

static void test_alerts_fixtures() {
    char routes[8][8];
    TEST_ASSERT_EQUAL_INT(2, routesOf(readFixture("alerts.json"), routes, 8));  // station "41320" ignored
    TEST_ASSERT_EQUAL_STRING("Red", routes[0]);
    TEST_ASSERT_EQUAL_STRING("22", routes[1]);  // its Service is a lone object
    TEST_ASSERT_EQUAL_INT(1, routesOf(readFixture("alerts_single.json"), routes, 8));
    TEST_ASSERT_EQUAL_STRING("Brn", routes[0]);
}

static void test_alerts_distinct_capped_and_shapes() {
    char routes[3][8];
    const std::string a = alerts("[" + alert("[" + svc("R", "Red") + "," + svc("B", "22") + "]") + "," +
                                 alert(svc("R", "Red")) + "," + alert("[" + svc("B", "36") + "," + svc("R", "G") + "]") + "]");
    TEST_ASSERT_EQUAL_INT(3, routesOf(a, routes, 3));  // Red once; capped at max
    TEST_ASSERT_EQUAL_STRING("Red", routes[0]);
    TEST_ASSERT_EQUAL_STRING("22", routes[1]);
    TEST_ASSERT_EQUAL_STRING("36", routes[2]);
    TEST_ASSERT_EQUAL_INT(0, routesOf(a, routes, 0));
    // an alert with no ImpactedService, or a non-route service, adds nothing
    TEST_ASSERT_EQUAL_INT(0, routesOf(alerts("[{\"AlertId\":\"9\"}," + alert(svc("T", "41320")) + "]"), routes, 3));
    TEST_ASSERT_EQUAL_INT(1, routesOf(alerts(alert(svc("B", "151"))), routes, 3));
    TEST_ASSERT_EQUAL_STRING("151", routes[0]);
    char cut[1][8];
    TEST_ASSERT_EQUAL_INT(1, routesOf(alerts(alert(svc("B", "VERYLONGROUTE"))), cut, 1));
    TEST_ASSERT_EQUAL_STRING("VERYLON", cut[0]);  // cut to 7 bytes, like record routes
}

static void test_alerts_empty_and_errors() {
    char routes[4][8];
    // no Alert (CTA's "no alerts" reply) is zero routes, whatever ErrorCode says, as in the app
    TEST_ASSERT_EQUAL_INT(0, routesOf("{\"CTAAlerts\":{\"ErrorCode\":\"50\",\"ErrorMessage\":\"No alerts\"}}", routes, 4));
    TEST_ASSERT_EQUAL_INT(0, routesOf(alerts("[]"), routes, 4));
    TEST_ASSERT_EQUAL_INT(-1, routesOf("{\"CTAAlerts\":", routes, 4));
    TEST_ASSERT_EQUAL_INT(-1, routesOf("{\"other\":1}", routes, 4));
    TEST_ASSERT_EQUAL_INT(-1, cta::parseAlertRoutes(nullptr, 0, routes, 4));
}

static void test_alerts_stay_filtered() {
    unsigned& passes = cta::json::unfilteredPasses();
    char routes[8][8];
    passes = 0;
    routesOf(readFixture("alerts.json"), routes, 8);  // mixes a Service list and a lone Service
    routesOf(readFixture("alerts_single.json"), routes, 8);  // a lone Alert
    TEST_ASSERT_EQUAL_UINT(0, passes);
}

static void test_weather() {
    const std::string wx = readFixture("weather.json");
    int w = 0;
    float t = 0;
    TEST_ASSERT_TRUE(cta::parseWeather(wx.c_str(), wx.size(), w, t));
    TEST_ASSERT_EQUAL_INT(61, w);
    TEST_ASSERT_EQUAL_FLOAT(-2.5f, t);
    const std::string intTemp = "{\"current\":{\"temperature_2m\":3,\"weather_code\":0}}";
    TEST_ASSERT_TRUE(cta::parseWeather(intTemp.c_str(), intTemp.size(), w, t));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, t);
    w = 42;
    t = 42;
    for (const char* bad : {"{\"current\":{\"temperature_2m\":3}}", "{\"current\":{\"weather_code\":3}}",
                            "{\"current\":{\"temperature_2m\":\"3\",\"weather_code\":3}}",
                            "{\"current\":{\"temperature_2m\":3,\"weather_code\":2.5}}", "{\"error\":true,\"reason\":\"x\"}",
                            "{\"current\":"}) {
        TEST_ASSERT_FALSE(cta::parseWeather(bad, strlen(bad), w, t));
    }
    TEST_ASSERT_FALSE(cta::parseWeather(nullptr, 0, w, t));
    TEST_ASSERT_EQUAL_INT(42, w);  // untouched on failure
    TEST_ASSERT_EQUAL_FLOAT(42.0f, t);
}

static void test_accessory() {
    using cta::Accessory;
    TEST_ASSERT_EQUAL((int)Accessory::Umbrella, (int)cta::accessoryFor(61, 5));
    TEST_ASSERT_EQUAL((int)Accessory::Umbrella, (int)cta::accessoryFor(61, -1));  // rain beats cold
    TEST_ASSERT_EQUAL((int)Accessory::Umbrella, (int)cta::accessoryFor(95, 20));
    TEST_ASSERT_EQUAL((int)Accessory::Scarf, (int)cta::accessoryFor(71, 1));
    TEST_ASSERT_EQUAL((int)Accessory::Scarf, (int)cta::accessoryFor(85, 0));
    TEST_ASSERT_EQUAL((int)Accessory::Scarf, (int)cta::accessoryFor(3, -1));
    TEST_ASSERT_EQUAL((int)Accessory::None, (int)cta::accessoryFor(2, 10));
    // every range edge (spec 6.3)
    for (int c : {51, 67, 80, 82, 95, 99}) TEST_ASSERT_EQUAL((int)Accessory::Umbrella, (int)cta::accessoryFor(c, 10));
    for (int c : {71, 77, 85, 86}) TEST_ASSERT_EQUAL((int)Accessory::Scarf, (int)cta::accessoryFor(c, 10));
    for (int c : {50, 68, 70, 78, 79, 83, 84, 87, 94, 100})
        TEST_ASSERT_EQUAL((int)Accessory::None, (int)cta::accessoryFor(c, 10));
    TEST_ASSERT_EQUAL((int)Accessory::None, (int)cta::accessoryFor(0, 0.0f));  // 0 °C is not below 0
    TEST_ASSERT_EQUAL((int)Accessory::Scarf, (int)cta::accessoryFor(0, -0.1f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_alerts_fixtures);
    RUN_TEST(test_alerts_distinct_capped_and_shapes);
    RUN_TEST(test_alerts_empty_and_errors);
    RUN_TEST(test_alerts_stay_filtered);
    RUN_TEST(test_weather);
    RUN_TEST(test_accessory);
    return UNITY_END();
}
