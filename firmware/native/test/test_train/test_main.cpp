// Train Tracker arrivals and positions parsing (plan 1, task 4).
#include <unity.h>

#include <string.h>

#include <initializer_list>
#include <string>

#include "cta_types.h"
#include "train_parse.h"
#include "../fixture.h"

void setUp() {}
void tearDown() {}

static cta::StopRecord fresh(const char* id) {
    cta::StopRecord r{};
    strcpy(r.id, id);
    strcpy(r.dir, "caller dir");
    r.alert = true;
    return r;
}

static bool parse(const std::string& json, uint32_t now, cta::StopRecord& r) {
    return cta::parseTrainArrivals(json.c_str(), json.size(), now, r);
}

static std::string response(const char* tmst, const std::string& eta) {
    return std::string("{\"ctatt\":{\"tmst\":\"") + tmst + "\",\"errCd\":\"0\",\"errNm\":null,\"eta\":" + eta + "}}";
}

static std::string eta(const char* rt, const char* dest, const char* arrT, const char* isSch = "0",
                       const char* isDly = "0", const char* staNm = "Belmont") {
    return std::string("{\"staId\":\"41320\",\"stpId\":\"30255\",\"staNm\":\"") + staNm + "\",\"rt\":\"" + rt +
           "\",\"destNm\":\"" + dest + "\",\"arrT\":\"" + arrT + "\",\"isSch\":\"" + isSch + "\",\"isDly\":\"" +
           isDly + "\",\"isApp\":\"0\",\"lat\":null}";
}

static void test_station_fixture() {
    const std::string f = readFixture("train_arrivals_station.json");
    cta::StopRecord r = fresh("41320");
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(f.c_str(), f.size(), 1000, r));
    TEST_ASSERT_EQUAL_UINT8(4, r.count);  // Linden at 20 min dropped
    TEST_ASSERT_EQUAL_INT16(1, r.arrivals[0].min);
    TEST_ASSERT_EQUAL_STRING("Kimball", r.arrivals[0].dest);
    TEST_ASSERT_EQUAL_INT16(3, r.arrivals[1].min);
    TEST_ASSERT_TRUE(r.arrivals[1].live);
    TEST_ASSERT_EQUAL_INT16(7, r.arrivals[2].min);
    TEST_ASSERT_TRUE(r.arrivals[2].delayed);
    TEST_ASSERT_EQUAL_INT16(12, r.arrivals[3].min);
    TEST_ASSERT_FALSE(r.arrivals[3].live);
    TEST_ASSERT_EQUAL_STRING("Belmont", r.name);
    TEST_ASSERT_EQUAL_STRING("41320", r.id);
    TEST_ASSERT_EQUAL_UINT32(1000, r.ts);
    TEST_ASSERT_EQUAL_INT((int)cta::Kind::Train, (int)r.type);
    TEST_ASSERT_EQUAL_STRING("Brn", r.route);  // rt of the soonest arrival
    TEST_ASSERT_EQUAL_STRING("caller dir", r.dir);  // left as the caller set it
    TEST_ASSERT_TRUE(r.alert);
}

static void test_midnight_fixture() {
    const std::string f = readFixture("train_arrivals_midnight.json");
    cta::StopRecord r = fresh("41320");
    TEST_ASSERT_TRUE(cta::parseTrainArrivals(f.c_str(), f.size(), 1, r));
    TEST_ASSERT_EQUAL_UINT8(1, r.count);
    TEST_ASSERT_EQUAL_INT16(5, r.arrivals[0].min);
}

static void test_error_fixture() {
    const std::string f = readFixture("train_error.json");
    cta::StopRecord r = fresh("41320");
    strcpy(r.name, "kept");
    TEST_ASSERT_FALSE(cta::parseTrainArrivals(f.c_str(), f.size(), 1, r));
    TEST_ASSERT_EQUAL_STRING("kept", r.name);  // untouched on failure
    TEST_ASSERT_EQUAL_UINT32(0, r.ts);
}

static void test_single_eta_object() {
    cta::StopRecord r = fresh("41320");
    TEST_ASSERT_TRUE(parse(response("2026-11-05T08:00:00", eta("Red", "Howard", "2026-11-05T08:04:00")), 5, r));
    TEST_ASSERT_EQUAL_UINT8(1, r.count);
    TEST_ASSERT_EQUAL_INT16(4, r.arrivals[0].min);
    TEST_ASSERT_EQUAL_STRING("Howard", r.arrivals[0].dest);
    TEST_ASSERT_EQUAL_STRING("Red", r.route);
}

static void test_no_arrivals() {
    for (const char* etaJson : {"null", "[]"}) {
        cta::StopRecord r = fresh("41320");
        strcpy(r.name, "Belmont");
        TEST_ASSERT_TRUE(parse(response("2026-11-05T08:00:00", etaJson), 7, r));
        TEST_ASSERT_EQUAL_UINT8(0, r.count);
        TEST_ASSERT_EQUAL_UINT32(7, r.ts);
        TEST_ASSERT_EQUAL_STRING("Belmont", r.name);  // nothing to replace it with
    }
    const std::string noEta = "{\"ctatt\":{\"tmst\":\"2026-11-05T08:00:00\",\"errCd\":\"0\"}}";
    cta::StopRecord r = fresh("41320");
    TEST_ASSERT_TRUE(parse(noEta, 7, r));
    TEST_ASSERT_EQUAL_UINT8(0, r.count);
}

static void test_rejects() {
    cta::StopRecord r = fresh("41320");
    TEST_ASSERT_FALSE(parse("{\"ctatt\":", 1, r));
    TEST_ASSERT_FALSE(parse("[]", 1, r));
    TEST_ASSERT_FALSE(parse("{\"ctatt\":{\"tmst\":\"2026-11-05T08:00:00\",\"eta\":[]}}", 1, r));  // no errCd
    // arrivals but no usable response time: minutes can't be counted
    TEST_ASSERT_FALSE(parse("{\"ctatt\":{\"errCd\":\"0\",\"eta\":[" + eta("Red", "Howard", "2026-11-05T08:04:00") + "]}}", 1, r));
    TEST_ASSERT_FALSE(parse(response("garbage", "[" + eta("Red", "Howard", "2026-11-05T08:04:00") + "]"), 1, r));
    TEST_ASSERT_FALSE(cta::parseTrainArrivals(nullptr, 0, 1, r));
    TEST_ASSERT_EQUAL_UINT32(0, r.ts);
}

static void test_skips_bad_arrivals_and_clamps() {
    cta::StopRecord r = fresh("41320");
    const std::string body = "[" + eta("Red", "Howard", "not a time") + "," +
                             eta("Red", "95th/Dan Ryan", "2026-11-05T07:58:00") + "," +
                             eta("Brn", "Kimball", "2026-11-05T08:02:59") + "]";
    TEST_ASSERT_TRUE(parse(response("2026-11-05T08:00:00", body), 1, r));
    TEST_ASSERT_EQUAL_UINT8(2, r.count);
    TEST_ASSERT_EQUAL_INT16(0, r.arrivals[0].min);  // a prediction in the past reads 0, never negative
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", r.arrivals[0].dest);
    TEST_ASSERT_EQUAL_INT16(2, r.arrivals[1].min);  // 2:59 floors to 2
}

static void test_sorts_by_seconds_and_keeps_four() {
    cta::StopRecord r = fresh("41320");
    std::string body = "[";
    const char* times[] = {"08:09:00", "08:03:40", "08:03:10", "08:30:00", "08:01:00", "08:05:00"};
    const char* dests[] = {"A", "B", "C", "D", "E", "F"};
    for (int i = 0; i < 6; ++i) {
        if (i) body += ",";
        body += eta("Red", dests[i], (std::string("2026-11-05T") + times[i]).c_str());
    }
    body += "]";
    TEST_ASSERT_TRUE(parse(response("2026-11-05T08:00:00", body), 1, r));
    TEST_ASSERT_EQUAL_UINT8(4, r.count);
    TEST_ASSERT_EQUAL_STRING("E", r.arrivals[0].dest);
    TEST_ASSERT_EQUAL_STRING("C", r.arrivals[1].dest);  // 3:10 before 3:40, though both read 3 min
    TEST_ASSERT_EQUAL_STRING("B", r.arrivals[2].dest);
    TEST_ASSERT_EQUAL_STRING("F", r.arrivals[3].dest);
}

static void test_daylight_saving() {
    cta::StopRecord r = fresh("41320");
    // fall-back night: a response at 1:50 CDT, a train at 1:25 CST, 35 min later (spec 6.6)
    TEST_ASSERT_TRUE(parse(response("2026-11-01T01:50:00", eta("Red", "Howard", "2026-11-01T01:25:00")), 1, r));
    TEST_ASSERT_EQUAL_INT16(35, r.arrivals[0].min);
    // spring-forward night: 1:58 → 3:03 is 5 min
    TEST_ASSERT_TRUE(parse(response("2026-03-08T01:58:00", eta("Red", "Howard", "2026-03-08T03:03:00")), 1, r));
    TEST_ASSERT_EQUAL_INT16(5, r.arrivals[0].min);
}

static void test_truncates_names() {
    cta::StopRecord r = fresh("40380");
    TEST_ASSERT_TRUE(parse(response("2026-11-05T08:00:00",
                                    eta("Blue", "Forest Park (via the Congress Branch)", "2026-11-05T08:04:00", "0",
                                        "0", "Clark/Lake (Blue/Brown/Green/Orange/Purple/Pink)")),
                           1, r));
    TEST_ASSERT_EQUAL_STRING("Clark/Lake (Blue/Brown/G", r.name);
    TEST_ASSERT_EQUAL_STRING("Forest Park (via the Con", r.arrivals[0].dest);
}

static void test_positions() {
    cta::TrainPos pos[8];
    std::string f = readFixture("train_positions_red.json");
    TEST_ASSERT_EQUAL_INT(2, cta::parseTrainPositions(f.c_str(), f.size(), pos, 8));
    TEST_ASSERT_EQUAL_STRING("30005", pos[0].nextStpId);
    TEST_ASSERT_EQUAL_STRING("30001", pos[1].nextStpId);
    TEST_ASSERT_EQUAL_INT(1, cta::parseTrainPositions(f.c_str(), f.size(), pos, 1));  // max respected
    f = readFixture("train_positions_single.json");
    TEST_ASSERT_EQUAL_INT(1, cta::parseTrainPositions(f.c_str(), f.size(), pos, 8));
    TEST_ASSERT_EQUAL_STRING("30005", pos[0].nextStpId);
    f = readFixture("train_error.json");
    TEST_ASSERT_EQUAL_INT(-1, cta::parseTrainPositions(f.c_str(), f.size(), pos, 8));
    TEST_ASSERT_EQUAL_INT(-1, cta::parseTrainPositions("{\"ctatt\":", 9, pos, 8));
}

static void test_positions_shapes() {
    cta::TrainPos pos[4];
    // route as a single object, a train with no nextStpId, and no trains at all
    const std::string oneRoute =
        "{\"ctatt\":{\"errCd\":\"0\",\"route\":{\"@name\":\"red\",\"train\":[{\"rn\":\"1\",\"nextStpId\":\"30005\"},"
        "{\"rn\":\"2\"}]}}}";
    TEST_ASSERT_EQUAL_INT(2, cta::parseTrainPositions(oneRoute.c_str(), oneRoute.size(), pos, 4));
    TEST_ASSERT_EQUAL_STRING("30005", pos[0].nextStpId);
    TEST_ASSERT_EQUAL_STRING("", pos[1].nextStpId);
    const std::string none = "{\"ctatt\":{\"errCd\":\"0\",\"route\":[{\"@name\":\"red\"}]}}";
    TEST_ASSERT_EQUAL_INT(0, cta::parseTrainPositions(none.c_str(), none.size(), pos, 4));
    TEST_ASSERT_EQUAL_INT(0, cta::parseTrainPositions(oneRoute.c_str(), oneRoute.size(), pos, 0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_station_fixture);
    RUN_TEST(test_midnight_fixture);
    RUN_TEST(test_error_fixture);
    RUN_TEST(test_single_eta_object);
    RUN_TEST(test_no_arrivals);
    RUN_TEST(test_rejects);
    RUN_TEST(test_skips_bad_arrivals_and_clamps);
    RUN_TEST(test_sorts_by_seconds_and_keeps_four);
    RUN_TEST(test_daylight_saving);
    RUN_TEST(test_truncates_names);
    RUN_TEST(test_positions);
    RUN_TEST(test_positions_shapes);
    return UNITY_END();
}
