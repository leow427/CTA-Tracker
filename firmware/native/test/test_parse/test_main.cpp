#include <math.h>
#include <string.h>
#include <unity.h>

#include "../fixture.h"
#include "alerts_weather.h"
#include "bus_parse.h"
#include "train_parse.h"

void setUp() {}
void tearDown() {}

static void test_train_arrivals() {
    std::string f = readFixture("train_arrivals_station.json");
    cta::StopRecord r{};
    strcpy(r.id, "41320");
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(f.c_str(), f.size(), 1000, nullptr, r));
    TEST_ASSERT_EQUAL_UINT8(4, r.count);   // Linden at 20 min dropped
    TEST_ASSERT_EQUAL_INT16(1, r.arrivals[0].min);
    TEST_ASSERT_EQUAL_STRING("Kimball", r.arrivals[0].dest);
    TEST_ASSERT_EQUAL_INT16(3, r.arrivals[1].min);
    TEST_ASSERT_TRUE(r.arrivals[1].live);
    TEST_ASSERT_EQUAL_INT16(7, r.arrivals[2].min);
    TEST_ASSERT_TRUE(r.arrivals[2].delayed);
    TEST_ASSERT_EQUAL_INT16(12, r.arrivals[3].min);
    TEST_ASSERT_FALSE(r.arrivals[3].live);
    TEST_ASSERT_EQUAL_STRING("Brn", r.route);
    TEST_ASSERT_EQUAL_STRING("Belmont", r.name);
    TEST_ASSERT_EQUAL_STRING("41320", r.id);
    TEST_ASSERT_EQUAL_UINT32(1000, r.ts);
}

static void test_train_route_filter() {
    std::string f = readFixture("train_arrivals_station.json");
    cta::StopRecord r{};
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(f.c_str(), f.size(), 1000, "red", r));
    TEST_ASSERT_EQUAL_UINT8(3, r.count);
    TEST_ASSERT_EQUAL_STRING("Red", r.route);
    TEST_ASSERT_EQUAL_INT16(3, r.arrivals[0].min);
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(f.c_str(), f.size(), 1000, "Y", r));
    TEST_ASSERT_EQUAL_UINT8(0, r.count);
}

static void test_train_midnight_and_errors() {
    std::string f = readFixture("train_arrivals_midnight.json");
    cta::StopRecord r{};
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(f.c_str(), f.size(), 1, nullptr, r));
    TEST_ASSERT_EQUAL_INT16(5, r.arrivals[0].min);
    std::string e = readFixture("train_error.json");
    TEST_ASSERT_FALSE(cta::parseTrainArrivals(e.c_str(), e.size(), 1, nullptr, r));
    TEST_ASSERT_FALSE(cta::parseTrainArrivals("{\"ctatt\":", 9, 1, nullptr, r));
    const char* none = "{\"ctatt\":{\"tmst\":\"2026-11-05T08:00:00\",\"errCd\":\"0\",\"eta\":null}}";
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(none, strlen(none), 1, nullptr, r));
    TEST_ASSERT_EQUAL_UINT8(0, r.count);
    // a single eta object instead of an array
    const char* one = "{\"ctatt\":{\"tmst\":\"2026-11-05T08:00:00\",\"errCd\":\"0\",\"eta\":{\"staNm\":\"Belmont\","
                      "\"rt\":\"Red\",\"destNm\":\"Howard\",\"arrT\":\"2026-11-05T07:59:00\",\"isSch\":\"0\",\"isDly\":\"0\"}}}";
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(one, strlen(one), 1, nullptr, r));
    TEST_ASSERT_EQUAL_UINT8(1, r.count);
    TEST_ASSERT_EQUAL_INT16(0, r.arrivals[0].min);   // already past: clamped
}

static void test_train_positions() {
    cta::TrainPos t[8];
    std::string f = readFixture("train_positions_red.json");
    TEST_ASSERT_EQUAL_INT(2, cta::parseTrainPositions(f.c_str(), f.size(), t, 8));
    TEST_ASSERT_EQUAL_STRING("30005", t[0].nextStpId);
    TEST_ASSERT_EQUAL_STRING("red", t[0].route);   // from the route group's @name
    TEST_ASSERT_EQUAL_INT(1, cta::parseTrainPositions(f.c_str(), f.size(), t, 1));   // capacity respected
    std::string s = readFixture("train_positions_single.json");
    TEST_ASSERT_EQUAL_INT(1, cta::parseTrainPositions(s.c_str(), s.size(), t, 8));
    std::string e = readFixture("train_error.json");
    TEST_ASSERT_EQUAL_INT(-1, cta::parseTrainPositions(e.c_str(), e.size(), t, 8));
}

static cta::BusVehicle vsQuiet[4];

static void test_bus_predictions() {
    std::string f = readFixture("bus_predictions.json");
    cta::StopRecord recs[5];
    int n = cta::parseBusPredictions(f.c_str(), f.size(), 1000, nullptr, recs, 5);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("1001", recs[0].id);
    TEST_ASSERT_EQUAL_UINT8(3, recs[0].count);
    TEST_ASSERT_EQUAL_INT16(0, recs[0].arrivals[0].min);   // 36, DUE
    TEST_ASSERT_EQUAL_STRING("36", recs[0].route);
    TEST_ASSERT_EQUAL_INT16(4, recs[0].arrivals[1].min);
    TEST_ASSERT_TRUE(recs[0].arrivals[2].delayed);
    TEST_ASSERT_EQUAL_STRING("Westbound", recs[1].dir);
    TEST_ASSERT_EQUAL_UINT32(1000, recs[0].ts);
    TEST_ASSERT_EQUAL_UINT32(1000, recs[1].ts);
    TEST_ASSERT_TRUE(recs[1].type == cta::Kind::Bus);
    n = cta::parseBusPredictions(f.c_str(), f.size(), 1000, "22", recs, 5);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8(2, recs[0].count);
    TEST_ASSERT_EQUAL_STRING("Southbound", recs[0].dir);
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusPredictions(f.c_str(), f.size(), 1000, nullptr, recs, 1));
    std::string none = readFixture("bus_no_arrivals.json");
    TEST_ASSERT_EQUAL_INT(0, cta::parseBusPredictions(none.c_str(), none.size(), 1000, nullptr, recs, 5));
    std::string key = readFixture("bus_error_key.json");
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusPredictions(key.c_str(), key.size(), 1000, nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusPredictions("[]", 2, 1000, nullptr, recs, 5));
    // one prediction as an object, not an array
    const char* one = "{\"bustime-response\":{\"prd\":{\"tmstmp\":\"20261105 08:00\",\"stpid\":\"1001\",\"stpnm\":\"Clark & Belmont\","
                      "\"rt\":\"22\",\"rtdir\":\"Southbound\",\"des\":\"Harrison\",\"prdtm\":\"20261105 08:06\",\"prdctdn\":\"6\",\"dly\":false}}}";
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusPredictions(one, strlen(one), 1000, nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT16(6, recs[0].arrivals[0].min);
    TEST_ASSERT_FALSE(recs[0].arrivals[0].delayed);
    // "DLY" alone marks a prediction delayed, even with dly false
    const char* dly = "{\"bustime-response\":{\"prd\":{\"tmstmp\":\"20261105 08:00\",\"stpid\":\"1001\",\"stpnm\":\"Clark & Belmont\","
                      "\"rt\":\"22\",\"rtdir\":\"Southbound\",\"des\":\"Harrison\",\"prdtm\":\"20261105 08:06\",\"prdctdn\":\"DLY\",\"dly\":false}}}";
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusPredictions(dly, strlen(dly), 1000, nullptr, recs, 5));
    TEST_ASSERT_TRUE(recs[0].arrivals[0].delayed);
    TEST_ASSERT_TRUE(recs[0].arrivals[0].live);   // Bus Tracker only predicts live
    // A prediction whose time doesn't parse is dropped, never half-built (its stop is left out)
    const char* badTime = "{\"bustime-response\":{\"prd\":[{\"tmstmp\":\"20261105 08:00\",\"stpid\":\"1001\",\"rt\":\"22\","
                          "\"rtdir\":\"Southbound\",\"des\":\"Harrison\",\"prdtm\":\"20261105 8:04\",\"prdctdn\":\"4\"},"
                          "{\"stpid\":\"1002\",\"rt\":\"22\",\"rtdir\":\"Northbound\",\"des\":\"Howard\",\"prdtm\":\"\",\"prdctdn\":\"3\"},"
                          "{\"tmstmp\":\"20261105 08:00\",\"stpid\":\"1003\",\"rt\":\"36\",\"rtdir\":\"Southbound\",\"des\":\"Harrison\","
                          "\"prdtm\":\"20261105 08:05\",\"prdctdn\":\"5\"}]}}";
    memset(recs, 0xA5, sizeof recs);
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusPredictions(badTime, strlen(badTime), 1000, nullptr, recs, 1));
    TEST_ASSERT_EQUAL_STRING("1003", recs[0].id);
    TEST_ASSERT_EQUAL_UINT8(1, recs[0].count);
    TEST_ASSERT_EQUAL_STRING("36", recs[0].route);
    // An error naming a route ("No data found", getvehicles overnight) means nothing there, not a failure
    const char* quiet = "{\"bustime-response\":{\"error\":[{\"rt\":\"22\",\"msg\":\"No data found for parameter\"}]}}";
    TEST_ASSERT_EQUAL_INT(0, cta::parseBusVehicles(quiet, strlen(quiet), vsQuiet, 4));
}

static void test_bus_vehicles_and_picker() {
    cta::BusVehicle vs[10];
    std::string v = readFixture("bus_vehicles.json");
    TEST_ASSERT_EQUAL_INT(3, cta::parseBusVehicles(v.c_str(), v.size(), vs, 10));
    TEST_ASSERT_EQUAL_INT32(100, vs[1].pid);
    TEST_ASSERT_EQUAL_INT32(1500, vs[1].pdist);
    cta::BusRoute routes[4];
    std::string r = readFixture("bus_routes.json");
    TEST_ASSERT_EQUAL_INT(3, cta::parseBusRoutes(r.c_str(), r.size(), routes, 4));
    TEST_ASSERT_EQUAL_STRING("22", routes[1].rt);
    TEST_ASSERT_EQUAL_STRING("Ashland Express (Owl Ser", routes[2].name);
    char dirs[4][cta::kNameLen + 1];
    std::string d = readFixture("bus_directions.json");
    TEST_ASSERT_EQUAL_INT(2, cta::parseBusDirections(d.c_str(), d.size(), dirs, 4));
    TEST_ASSERT_EQUAL_STRING("Southbound", dirs[1]);
    const char* v3 = "{\"bustime-response\":{\"directions\":{\"id\":\"Eastbound\",\"name\":\"Eastbound\"}}}";
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusDirections(v3, strlen(v3), dirs, 4));
    TEST_ASSERT_EQUAL_STRING("Eastbound", dirs[0]);
    cta::BusStop stops[4];
    std::string s = readFixture("bus_stops.json");
    TEST_ASSERT_EQUAL_INT(2, cta::parseBusStops(s.c_str(), s.size(), stops, 4));
    TEST_ASSERT_EQUAL_STRING("Clark & Diversey", stops[1].name);
    std::string key = readFixture("bus_error_key.json");
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusStops(key.c_str(), key.size(), stops, 4));
}

static void test_alerts_weather_geoip() {
    char routes[8][cta::kIdLen + 1];
    std::string a = readFixture("alerts.json");
    TEST_ASSERT_EQUAL_INT(2, cta::parseAlertRoutes(a.c_str(), a.size(), routes, 8));   // station 41320 ignored
    TEST_ASSERT_EQUAL_STRING("Red", routes[0]);
    TEST_ASSERT_EQUAL_STRING("22", routes[1]);
    std::string s = readFixture("alerts_single.json");
    TEST_ASSERT_EQUAL_INT(1, cta::parseAlertRoutes(s.c_str(), s.size(), routes, 8));
    TEST_ASSERT_EQUAL_STRING("Brn", routes[0]);
    TEST_ASSERT_EQUAL_INT(-1, cta::parseAlertRoutes("nope", 4, routes, 8));
    std::string none = readFixture("alerts_none.json");   // recorded: a quiet route is ErrorCode 50, no Alert
    TEST_ASSERT_EQUAL_INT(0, cta::parseAlertRoutes(none.c_str(), none.size(), routes, 8));
    const char* dup = "{\"CTAAlerts\":{\"Alert\":[{\"ImpactedService\":{\"Service\":[{\"ServiceType\":\"R\",\"ServiceId\":\"Red\"},"
                      "{\"ServiceType\":\"B\",\"ServiceId\":\"22\"}]}},{\"ImpactedService\":{\"Service\":{\"ServiceType\":\"R\",\"ServiceId\":\"Red\"}}}]}}";
    TEST_ASSERT_EQUAL_INT(2, cta::parseAlertRoutes(dup, strlen(dup), routes, 8));   // Red counted once
    char one[2][cta::kIdLen + 1] = {"", "guard"};
    TEST_ASSERT_EQUAL_INT(1, cta::parseAlertRoutes(dup, strlen(dup), one, 1));      // never past max
    TEST_ASSERT_EQUAL_STRING("guard", one[1]);
    int w;
    float t;
    std::string wx = readFixture("weather.json");
    TEST_ASSERT_TRUE(cta::parseWeather(wx.c_str(), wx.size(), w, t));
    TEST_ASSERT_EQUAL_INT(61, w);
    TEST_ASSERT_EQUAL_FLOAT(-2.5f, t);
    TEST_ASSERT_FALSE(cta::parseWeather("{}", 2, w, t));
    using cta::Accessory;
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(61, 5));
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(61, -1));   // rain beats cold
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(95, 20));
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(82, 20));
    TEST_ASSERT_EQUAL(Accessory::Scarf, cta::accessoryFor(71, 1));
    TEST_ASSERT_EQUAL(Accessory::Scarf, cta::accessoryFor(86, 0));
    TEST_ASSERT_EQUAL(Accessory::Scarf, cta::accessoryFor(85, 0));
    TEST_ASSERT_EQUAL(Accessory::Scarf, cta::accessoryFor(77, 1));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(70, 1));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(84, 1));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(87, 1));
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(51, 5));
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(67, 5));
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(80, 5));
    TEST_ASSERT_EQUAL(Accessory::Umbrella, cta::accessoryFor(99, 5));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(50, 5));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(83, 5));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(94, 5));
    TEST_ASSERT_EQUAL(Accessory::Scarf, cta::accessoryFor(3, -1));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(68, 10));
    TEST_ASSERT_EQUAL(Accessory::None, cta::accessoryFor(2, 0));
    double lat, lon;
    std::string g = readFixture("geoip.json");
    TEST_ASSERT_TRUE(cta::parseGeoIp(g.c_str(), g.size(), lat, lon));
    TEST_ASSERT_TRUE(fabs(lat - 41.8781) < 1e-4);
    TEST_ASSERT_TRUE(fabs(lon + 87.6298) < 1e-4);
    std::string ge = readFixture("geoip_error.json");
    TEST_ASSERT_FALSE(cta::parseGeoIp(ge.c_str(), ge.size(), lat, lon));
    const char* errWithCoords = "{\"error\":true,\"latitude\":1.0,\"longitude\":2.0}";
    TEST_ASSERT_FALSE(cta::parseGeoIp(errWithCoords, strlen(errWithCoords), lat, lon));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_train_arrivals);
    RUN_TEST(test_train_route_filter);
    RUN_TEST(test_train_midnight_and_errors);
    RUN_TEST(test_train_positions);
    RUN_TEST(test_bus_predictions);
    RUN_TEST(test_bus_vehicles_and_picker);
    RUN_TEST(test_alerts_weather_geoip);
    return UNITY_END();
}
