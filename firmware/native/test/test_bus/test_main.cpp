// Bus Tracker predictions and vehicles parsing (plan 1, task 5).
#include <unity.h>

#include <string.h>

#include <string>

#include "bus_parse.h"
#include "cta_types.h"
#include "json_util.h"
#include "../fixture.h"

void setUp() {}
void tearDown() {}

static int preds(const std::string& json, const char* filter, cta::StopRecord* recs, size_t max) {
    return cta::parseBusPredictions(json.c_str(), json.size(), 1000, filter, recs, max);
}

static std::string prd(const char* stpid, const char* rt, const char* prdtm, const char* prdctdn,
                       bool dly = false, const char* tmstmp = "20261105 08:00", const char* stpnm = "Clark & Belmont",
                       const char* des = "Harrison", const char* rtdir = "Southbound") {
    return std::string("{\"tmstmp\":\"") + tmstmp + "\",\"typ\":\"A\",\"stpnm\":\"" + stpnm + "\",\"stpid\":\"" +
           stpid + "\",\"rt\":\"" + rt + "\",\"rtdir\":\"" + rtdir + "\",\"des\":\"" + des + "\",\"prdtm\":\"" +
           prdtm + "\",\"dly\":" + (dly ? "true" : "false") + ",\"prdctdn\":\"" + prdctdn + "\"}";
}

static std::string body(const std::string& inner) { return "{\"bustime-response\":{" + inner + "}}"; }

static void test_predictions_fixture() {
    const std::string f = readFixture("bus_predictions.json");
    cta::StopRecord recs[5];
    // one record per (stop, route), in order of first appearance (spec 6.2): 1001/22, 1001/36, 1002/77
    int n = preds(f, nullptr, recs, 5);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("1001", recs[0].id);
    TEST_ASSERT_EQUAL_STRING("22", recs[0].route);
    TEST_ASSERT_EQUAL_STRING("Southbound", recs[0].dir);
    TEST_ASSERT_EQUAL_STRING("Clark & Belmont", recs[0].name);
    TEST_ASSERT_EQUAL_UINT8(2, recs[0].count);
    TEST_ASSERT_EQUAL_INT16(4, recs[0].arrivals[0].min);
    TEST_ASSERT_FALSE(recs[0].arrivals[0].delayed);
    TEST_ASSERT_TRUE(recs[0].arrivals[0].live);
    TEST_ASSERT_EQUAL_STRING("Harrison", recs[0].arrivals[0].dest);  // where it ends
    TEST_ASSERT_EQUAL_INT16(15, recs[0].arrivals[1].min);
    TEST_ASSERT_TRUE(recs[0].arrivals[1].delayed);  // DLY
    TEST_ASSERT_EQUAL_INT((int)cta::Kind::Bus, (int)recs[0].type);
    TEST_ASSERT_EQUAL_UINT32(1000, recs[0].ts);
    TEST_ASSERT_FALSE(recs[0].alert);
    TEST_ASSERT_EQUAL_STRING("1001", recs[1].id);
    TEST_ASSERT_EQUAL_STRING("36", recs[1].route);
    TEST_ASSERT_EQUAL_UINT8(1, recs[1].count);
    TEST_ASSERT_EQUAL_INT16(0, recs[1].arrivals[0].min);  // DUE
    TEST_ASSERT_EQUAL_STRING("1002", recs[2].id);
    TEST_ASSERT_EQUAL_STRING("77", recs[2].route);
    TEST_ASSERT_EQUAL_STRING("Westbound", recs[2].dir);
    TEST_ASSERT_EQUAL_STRING("Cumberland", recs[2].arrivals[0].dest);
    TEST_ASSERT_EQUAL_INT16(9, recs[2].arrivals[0].min);
    n = preds(f, "22", recs, 5);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8(2, recs[0].count);
    TEST_ASSERT_EQUAL_STRING("22", recs[0].route);
    TEST_ASSERT_EQUAL_INT(1, preds(f, nullptr, recs, 1));  // max respected: the first card only
    TEST_ASSERT_EQUAL_STRING("22", recs[0].route);
    TEST_ASSERT_EQUAL_UINT8(2, recs[0].count);  // later predictions for a kept card still count
    TEST_ASSERT_EQUAL_INT(0, preds(f, "151", recs, 5));
}

static void test_one_record_per_route_at_a_stop() {
    cta::StopRecord recs[4];
    const std::string b = body("\"prd\":[" + prd("1001", "22", "20261105 08:06", "6") + "," +
                               prd("1001", "36", "20261105 08:02", "2", false, "20261105 08:00", "Clark & Belmont",
                                   "Broadway") + "," +
                               prd("1002", "22", "20261105 08:03", "3", false, "20261105 08:00", "Belmont & Clark",
                                   "Howard", "Northbound") + "," +
                               prd("1001", "22", "20261105 08:01", "1") + "]");
    TEST_ASSERT_EQUAL_INT(3, preds(b, nullptr, recs, 4));
    TEST_ASSERT_EQUAL_STRING("1001", recs[0].id);
    TEST_ASSERT_EQUAL_STRING("22", recs[0].route);
    TEST_ASSERT_EQUAL_UINT8(2, recs[0].count);
    TEST_ASSERT_EQUAL_INT16(1, recs[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_INT16(6, recs[0].arrivals[1].min);
    TEST_ASSERT_EQUAL_STRING("1001", recs[1].id);
    TEST_ASSERT_EQUAL_STRING("36", recs[1].route);
    TEST_ASSERT_EQUAL_STRING("Broadway", recs[1].arrivals[0].dest);
    TEST_ASSERT_EQUAL_STRING("1002", recs[2].id);  // the same route at another stop is its own card
    TEST_ASSERT_EQUAL_STRING("22", recs[2].route);
    TEST_ASSERT_EQUAL_STRING("Northbound", recs[2].dir);
}

static void test_errors() {
    cta::StopRecord recs[5];
    // "No arrival times" names a stop: nothing there right now
    TEST_ASSERT_EQUAL_INT(0, preds(readFixture("bus_no_arrivals.json"), nullptr, recs, 5));
    // a bad key (or the daily limit) names nothing: the fetch failed
    TEST_ASSERT_EQUAL_INT(-1, preds(readFixture("bus_error_key.json"), nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT(0, preds(body("\"error\":{\"rt\":\"22\",\"msg\":\"No service scheduled\"}"), nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT(-1, preds(body("\"error\":{\"msg\":\"Transaction limit for current day has been exceeded.\"}"), nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT(0, preds(body(""), nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT(-1, preds("{\"bustime-response\":", nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT(-1, preds("[]", nullptr, recs, 5));
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusPredictions(nullptr, 0, 1, nullptr, recs, 5));
}

static void test_lone_prediction_and_mixed() {
    cta::StopRecord recs[3];
    TEST_ASSERT_EQUAL_INT(1, preds(body("\"prd\":" + prd("1001", "22", "20261105 08:04", "4")), nullptr, recs, 3));
    TEST_ASSERT_EQUAL_INT16(4, recs[0].arrivals[0].min);
    // predictions win when an error is also present
    const std::string mixed =
        body("\"prd\":[" + prd("1001", "22", "20261105 08:04", "4") + "],\"error\":[{\"msg\":\"Invalid stop\"}]");
    TEST_ASSERT_EQUAL_INT(1, preds(mixed, nullptr, recs, 3));
}

static void test_skips_and_orders() {
    cta::StopRecord recs[3];
    const std::string b = body("\"prd\":[" + prd("1001", "22", "garbage", "4") + "," +
                               prd("1001", "22", "20261105 08:07:40", "7") + "," +
                               prd("1001", "22", "20261105 08:07:10", "7", false, "20261105 08:00",
                                   "Clark & Belmont", "Pershing") + "," +
                               prd("1001", "22", "20261105 07:58", "DUE", false, "20261105 08:00",
                                   "Clark & Belmont", "Broadway") + "," +
                               prd("1001", "22", "20261105 07:59", "1", false, "20261105 08:00",
                                   "Clark & Belmont", "Howard", "Northbound") + "]");
    TEST_ASSERT_EQUAL_INT(1, preds(b, nullptr, recs, 3));
    TEST_ASSERT_EQUAL_UINT8(4, recs[0].count);  // the unparseable one is skipped
    // an already-passed prediction (−60 s) sorts before DUE (0 s), as in the app; both read 0 min
    TEST_ASSERT_EQUAL_INT16(0, recs[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_STRING("Howard", recs[0].arrivals[0].dest);
    TEST_ASSERT_EQUAL_STRING("22", recs[0].route);
    TEST_ASSERT_EQUAL_STRING("Northbound", recs[0].dir);  // dir from the soonest
    TEST_ASSERT_EQUAL_INT16(0, recs[0].arrivals[1].min);
    TEST_ASSERT_EQUAL_STRING("Broadway", recs[0].arrivals[1].dest);  // DUE
    // 08:07:10 before 08:07:40, though both read 7 min
    TEST_ASSERT_EQUAL_INT16(7, recs[0].arrivals[2].min);
    TEST_ASSERT_EQUAL_STRING("Pershing", recs[0].arrivals[2].dest);
    TEST_ASSERT_EQUAL_INT16(7, recs[0].arrivals[3].min);
    TEST_ASSERT_EQUAL_STRING("Harrison", recs[0].arrivals[3].dest);
}

static void test_keeps_four_soonest_per_stop() {
    cta::StopRecord recs[2];
    std::string list;
    const char* times[] = {"08:20", "08:05", "08:12", "08:02", "08:30", "08:09"};
    for (int i = 0; i < 6; ++i) {
        if (i) list += ",";
        list += prd("1001", "22", (std::string("20261105 ") + times[i]).c_str(), "x");
    }
    TEST_ASSERT_EQUAL_INT(1, preds(body("\"prd\":[" + list + "]"), nullptr, recs, 2));
    TEST_ASSERT_EQUAL_UINT8(4, recs[0].count);
    TEST_ASSERT_EQUAL_INT16(2, recs[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_INT16(5, recs[0].arrivals[1].min);
    TEST_ASSERT_EQUAL_INT16(9, recs[0].arrivals[2].min);
    TEST_ASSERT_EQUAL_INT16(12, recs[0].arrivals[3].min);
}

static void test_interleaved_stops_keep_their_own_order() {
    cta::StopRecord recs[3];
    // multi-stop responses can mix stops in time order: each stop's top 4 must use its own sort keys
    const std::string b = body("\"prd\":[" + prd("A", "22", "20261105 08:09", "9") + "," +
                               prd("B", "22", "20261105 08:05", "5") + "," +
                               prd("A", "22", "20261105 08:02", "2") + "," +
                               prd("B", "22", "20261105 08:12", "12") + "," +
                               prd("A", "22", "20261105 08:05", "5") + "]");
    TEST_ASSERT_EQUAL_INT(2, preds(b, nullptr, recs, 3));
    TEST_ASSERT_EQUAL_STRING("A", recs[0].id);
    TEST_ASSERT_EQUAL_UINT8(3, recs[0].count);
    TEST_ASSERT_EQUAL_INT16(2, recs[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_INT16(5, recs[0].arrivals[1].min);
    TEST_ASSERT_EQUAL_INT16(9, recs[0].arrivals[2].min);
    TEST_ASSERT_EQUAL_STRING("B", recs[1].id);
    TEST_ASSERT_EQUAL_UINT8(2, recs[1].count);
    TEST_ASSERT_EQUAL_INT16(5, recs[1].arrivals[0].min);
    TEST_ASSERT_EQUAL_INT16(12, recs[1].arrivals[1].min);
    // B's soonest must not decide where A's next arrival goes
    const std::string c = body("\"prd\":[" + prd("A", "22", "20261105 08:10", "10") + "," +
                               prd("B", "22", "20261105 08:01", "1") + "," +
                               prd("A", "22", "20261105 08:05", "5") + "]");
    TEST_ASSERT_EQUAL_INT(2, preds(c, nullptr, recs, 3));
    TEST_ASSERT_EQUAL_INT16(5, recs[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_INT16(10, recs[0].arrivals[1].min);
}

static void test_delayed_from_either_field() {
    cta::StopRecord recs[1];
    TEST_ASSERT_EQUAL_INT(1, preds(body("\"prd\":[" + prd("1", "22", "20261105 08:04", "4", true) + "]"), nullptr, recs, 1));
    TEST_ASSERT_TRUE(recs[0].arrivals[0].delayed);  // dly alone
    TEST_ASSERT_EQUAL_INT(1, preds(body("\"prd\":[" + prd("1", "22", "20261105 08:04", "DLY", false) + "]"), nullptr, recs, 1));
    TEST_ASSERT_TRUE(recs[0].arrivals[0].delayed);  // the DLY countdown alone
    TEST_ASSERT_EQUAL_INT16(4, recs[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_INT(1, preds(body("\"prd\":[" + prd("1", "22", "20261105 08:04", "4", false) + "]"), nullptr, recs, 1));
    TEST_ASSERT_FALSE(recs[0].arrivals[0].delayed);
}

static void test_daylight_saving_and_names() {
    cta::StopRecord recs[2];
    // fall-back night: a response at 1:50 CDT, a bus at 1:25 CST, 35 min later (spec 6.6)
    TEST_ASSERT_EQUAL_INT(1, preds(body("\"prd\":[" + prd("1001", "22", "20261101 01:25", "35", false, "20261101 01:50") + "]"),
                                   nullptr, recs, 2));
    TEST_ASSERT_EQUAL_INT16(35, recs[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_INT(1, preds(body("\"prd\":[" + prd("1001", "22", "20261105 08:04", "4", false, "20261105 08:00",
                                                           "Michigan & Chicago (Water Tower Place)",
                                                           "Harrison/Wells (Downtown via Michigan)") + "]"),
                                   nullptr, recs, 2));
    TEST_ASSERT_EQUAL_STRING("Michigan & Chicago (Wate", recs[0].name);
    TEST_ASSERT_EQUAL_STRING("Harrison/Wells (Downtown", recs[0].arrivals[0].dest);
}

static void test_vehicles() {
    cta::BusVehicle vs[10];
    const std::string v = readFixture("bus_vehicles.json");
    TEST_ASSERT_EQUAL_INT(3, cta::parseBusVehicles(v.c_str(), v.size(), vs, 10));
    TEST_ASSERT_EQUAL_INT32(100, vs[0].pid);
    TEST_ASSERT_EQUAL_INT32(4500, vs[0].pdist);
    TEST_ASSERT_EQUAL_INT32(100, vs[1].pid);  // numbers as strings
    TEST_ASSERT_EQUAL_INT32(1500, vs[1].pdist);
    TEST_ASSERT_EQUAL_INT32(200, vs[2].pid);
    TEST_ASSERT_EQUAL_INT(2, cta::parseBusVehicles(v.c_str(), v.size(), vs, 2));  // max respected
    const std::string key = readFixture("bus_error_key.json");
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusVehicles(key.c_str(), key.size(), vs, 10));
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusVehicles("{", 1, vs, 10));
}

static void test_vehicle_shapes() {
    cta::BusVehicle vs[4];
    std::string b = body("\"vehicle\":{\"vid\":\"1\",\"pid\":7,\"pdist\":\"250\"}");
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusVehicles(b.c_str(), b.size(), vs, 4));
    TEST_ASSERT_EQUAL_INT32(7, vs[0].pid);
    TEST_ASSERT_EQUAL_INT32(250, vs[0].pdist);
    // a vehicle without a usable pid or pdist can't be placed on a pattern: skipped
    b = body("\"vehicle\":[{\"pid\":\"abc\",\"pdist\":1},{\"pdist\":5},{\"pid\":9},{\"pid\":\"12\",\"pdist\":\"34x\"},"
             "{\"pid\":3,\"pdist\":40}]");
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusVehicles(b.c_str(), b.size(), vs, 4));
    TEST_ASSERT_EQUAL_INT32(3, vs[0].pid);
    b = body("\"error\":[{\"rt\":\"22\",\"msg\":\"No data found for parameter\"}]");
    TEST_ASSERT_EQUAL_INT(0, cta::parseBusVehicles(b.c_str(), b.size(), vs, 4));
}

static void test_big_responses_stay_filtered() {
    unsigned& passes = cta::json::unfilteredPasses();
    cta::StopRecord recs[5];
    cta::BusVehicle vs[10];
    passes = 0;
    TEST_ASSERT_EQUAL_INT(3, preds(readFixture("bus_predictions.json"), nullptr, recs, 5));
    const std::string v = readFixture("bus_vehicles.json");
    TEST_ASSERT_EQUAL_INT(3, cta::parseBusVehicles(v.c_str(), v.size(), vs, 10));
    TEST_ASSERT_EQUAL_UINT(0, passes);
    // a multi-stop response with per-stop errors stays filtered too
    const std::string mixed = body("\"prd\":[" + prd("1001", "22", "20261105 08:04", "4") +
                                   "],\"error\":[{\"stpid\":\"1002\",\"msg\":\"No arrival times\"}]");
    TEST_ASSERT_EQUAL_INT(1, preds(mixed, nullptr, recs, 5));
    TEST_ASSERT_EQUAL_UINT(0, passes);
    TEST_ASSERT_EQUAL_INT(0, preds(readFixture("bus_no_arrivals.json"), nullptr, recs, 5));
    TEST_ASSERT_EQUAL_UINT(1, passes);  // only the small shapes fall through
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_predictions_fixture);
    RUN_TEST(test_one_record_per_route_at_a_stop);
    RUN_TEST(test_errors);
    RUN_TEST(test_lone_prediction_and_mixed);
    RUN_TEST(test_skips_and_orders);
    RUN_TEST(test_keeps_four_soonest_per_stop);
    RUN_TEST(test_interleaved_stops_keep_their_own_order);
    RUN_TEST(test_delayed_from_either_field);
    RUN_TEST(test_daylight_saving_and_names);
    RUN_TEST(test_vehicles);
    RUN_TEST(test_vehicle_shapes);
    RUN_TEST(test_big_responses_stay_filtered);
    return UNITY_END();
}
