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

// Marks which of `wanted` are alerted; returns the count, or −1.
static int mark(const std::string& json, std::initializer_list<const char*> wanted, bool* alerted) {
    const char* w[16];
    size_t n = 0;
    for (const char* r : wanted) w[n++] = r;
    return cta::parseAlertRoutes(json.c_str(), json.size(), w, n, alerted);
}

static void test_alerts_fixtures() {
    bool a[4];
    TEST_ASSERT_EQUAL_INT(2, mark(readFixture("alerts.json"), {"Red", "22", "36", "41320"}, a));
    TEST_ASSERT_TRUE(a[0]);   // Red
    TEST_ASSERT_TRUE(a[1]);   // 22: its Service is a lone object
    TEST_ASSERT_FALSE(a[2]);  // 36: no alert
    TEST_ASSERT_FALSE(a[3]);  // station "41320" is a T service, not a route
    TEST_ASSERT_EQUAL_INT(1, mark(readFixture("alerts_single.json"), {"Red", "Brn"}, a));
    TEST_ASSERT_FALSE(a[0]);
    TEST_ASSERT_TRUE(a[1]);
}

static void test_alerts_mark_only_wanted_routes() {
    bool a[3];
    // a big event alert listing many routes can't crowd out the ones the caller tracks
    std::string many = "[";
    for (int r = 1; r <= 60; ++r) many += std::string(r > 1 ? "," : "") + svc("B", std::to_string(r).c_str());
    many += "]";
    TEST_ASSERT_EQUAL_INT(2, mark(alerts("[" + alert(many) + "," + alert(svc("R", "G")) + "]"), {"22", "Red", "G"}, a));
    TEST_ASSERT_TRUE(a[0]);
    TEST_ASSERT_FALSE(a[1]);
    TEST_ASSERT_TRUE(a[2]);
    // the same route in several alerts counts once
    TEST_ASSERT_EQUAL_INT(1, mark(alerts("[" + alert(svc("R", "Red")) + "," + alert("[" + svc("R", "Red") + "]") + "]"),
                                  {"Red"}, a));
    TEST_ASSERT_EQUAL_INT(0, mark(alerts(alert(svc("R", "Red"))), {}, a));
    // matching is exact, on routes cut to 7 bytes like record routes
    TEST_ASSERT_EQUAL_INT(1, mark(alerts(alert(svc("B", "VERYLONGROUTE"))), {"VERYLON", "2"}, a));
    TEST_ASSERT_TRUE(a[0]);
    TEST_ASSERT_FALSE(a[1]);
    TEST_ASSERT_EQUAL_INT(0, mark(alerts(alert(svc("B", "222"))), {"22"}, a));
}

static void test_alerts_skip_bad_services() {
    bool a[2];
    // no ImpactedService, a non-route service, a missing type or id, a numeric id: none mark anything
    const std::string b = alerts("[{\"AlertId\":\"9\"}," + alert(svc("T", "41320")) + "," +
                                 alert("[{\"ServiceId\":\"22\"},{\"ServiceType\":\"B\"},{\"ServiceType\":\"B\",\"ServiceId\":36}," +
                                       svc("B", "151") + "]") + "]");
    TEST_ASSERT_EQUAL_INT(1, mark(b, {"22", "151"}, a));
    TEST_ASSERT_FALSE(a[0]);
    TEST_ASSERT_TRUE(a[1]);
}

static void test_alerts_empty_and_errors() {
    bool a[2] = {true, true};
    // no Alert (CTA's "no alerts" reply) marks nothing, whatever ErrorCode says, as in the app
    TEST_ASSERT_EQUAL_INT(0, mark("{\"CTAAlerts\":{\"ErrorCode\":\"50\",\"ErrorMessage\":\"No alerts\"}}", {"Red", "22"}, a));
    TEST_ASSERT_FALSE(a[0]);
    TEST_ASSERT_FALSE(a[1]);
    TEST_ASSERT_EQUAL_INT(0, mark(alerts("[]"), {"Red"}, a));
    a[0] = true;
    TEST_ASSERT_EQUAL_INT(-1, mark("{\"CTAAlerts\":", {"Red"}, a));
    TEST_ASSERT_TRUE(a[0]);  // untouched on failure: the caller keeps its last alerts
    TEST_ASSERT_EQUAL_INT(-1, mark("{\"other\":1}", {"Red"}, a));
    const char* w[1] = {"Red"};
    TEST_ASSERT_EQUAL_INT(-1, cta::parseAlertRoutes(nullptr, 0, w, 1, a));
}

static void test_alerts_stay_filtered() {
    unsigned& passes = cta::json::unfilteredPasses();
    bool a[2];
    passes = 0;
    mark(readFixture("alerts.json"), {"Red", "22"}, a);  // mixes a Service list and a lone Service
    mark(readFixture("alerts_single.json"), {"Brn"}, a);  // a lone Alert
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
    RUN_TEST(test_alerts_mark_only_wanted_routes);
    RUN_TEST(test_alerts_skip_bad_services);
    RUN_TEST(test_alerts_empty_and_errors);
    RUN_TEST(test_alerts_stay_filtered);
    RUN_TEST(test_weather);
    RUN_TEST(test_accessory);
    return UNITY_END();
}
