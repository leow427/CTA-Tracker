// The dock's touch extras (ecosystem spec 5.10): a vehicle's stops ahead, and the stop the other way.
#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "../fixture.h"
#include "../json_bound.h"
#include "ahead.h"

void setUp() {}
void tearDown() {}

static int train(const std::string& j, cta::AheadStop* out, size_t max = cta::kAheadStops) {
    return cta::parseTrainAhead(j.c_str(), j.size(), out, max);
}
static int bus(const std::string& j, cta::AheadStop* out, size_t max = cta::kAheadStops) {
    return cta::parseBusAhead(j.c_str(), j.size(), out, max);
}

static void test_train_follow_fixture() {
    const std::string f = readFixture("train_follow.json");   // a real ttfollow reply, run 511 (Purple, to the Loop)
    cta::json::resetPeakBytes();
    cta::AheadStop s[cta::kAheadStops];
    TEST_ASSERT_EQUAL_INT(5, train(f, s));
    assertParseBounded(f);
    TEST_ASSERT_EQUAL_STRING("Belmont", s[0].name);
    TEST_ASSERT_EQUAL_INT16(0, s[0].min);   // 49 s
    TEST_ASSERT_EQUAL_STRING("Wellington", s[1].name);
    TEST_ASSERT_EQUAL_INT16(1, s[1].min);
    TEST_ASSERT_EQUAL_STRING("Diversey", s[2].name);   // the same time: reply order kept
    TEST_ASSERT_EQUAL_STRING("Fullerton", s[3].name);
    TEST_ASSERT_EQUAL_INT16(3, s[3].min);
    TEST_ASSERT_EQUAL_STRING("Armitage", s[4].name);
    TEST_ASSERT_EQUAL_INT16(5, s[4].min);
    TEST_ASSERT_EQUAL_INT(2, train(f, s, 2));
}

static void test_train_follow_gone_and_errors() {
    cta::AheadStop s[cta::kAheadStops];
    TEST_ASSERT_EQUAL_INT(0, train(readFixture("train_follow_none.json"), s));   // errCd 501: finished its run
    TEST_ASSERT_EQUAL_INT(-1, train(readFixture("train_error.json"), s));
    TEST_ASSERT_EQUAL_INT(-1, train("{\"ctatt\":", s));
    TEST_ASSERT_EQUAL_INT(0, train("{\"ctatt\":{\"tmst\":\"2026-11-16T16:35:52\",\"errCd\":\"0\",\"eta\":null}}", s));
    // one eta as an object
    TEST_ASSERT_EQUAL_INT(1, train("{\"ctatt\":{\"tmst\":\"2026-11-16T16:35:00\",\"errCd\":\"0\",\"eta\":"
                                   "{\"staNm\":\"Howard\",\"arrT\":\"2026-11-16T16:45:00\"}}}", s));
    TEST_ASSERT_EQUAL_STRING("Howard", s[0].name);
    TEST_ASSERT_EQUAL_INT16(10, s[0].min);
}

static void test_bus_follow_fixture() {
    const std::string f = readFixture("bus_follow.json");   // a real getpredictions?vid= reply, a 22 southbound
    cta::json::resetPeakBytes();
    cta::AheadStop s[cta::kAheadStops];
    TEST_ASSERT_EQUAL_INT(5, bus(f, s));
    assertParseBounded(f);
    TEST_ASSERT_EQUAL_STRING("Clark & Berwyn", s[0].name);
    TEST_ASSERT_EQUAL_INT16(0, s[0].min);   // DUE
    TEST_ASSERT_EQUAL_STRING("Clark & Foster", s[1].name);
    TEST_ASSERT_EQUAL_INT16(0, s[1].min);
    TEST_ASSERT_EQUAL_STRING("Clark & Winnemac", s[2].name);
    TEST_ASSERT_EQUAL_INT16(5, s[2].min);
    TEST_ASSERT_EQUAL_STRING("Clark & Lawrence", s[4].name);
    TEST_ASSERT_EQUAL_INT16(7, s[4].min);
}

static void test_bus_follow_gone_and_errors() {
    cta::AheadStop s[cta::kAheadStops];
    TEST_ASSERT_EQUAL_INT(0, bus(readFixture("bus_follow_none.json"), s));   // error names the vid: off the road
    TEST_ASSERT_EQUAL_INT(-1, bus(readFixture("bus_error_key.json"), s));
    TEST_ASSERT_EQUAL_INT(-1, bus(readFixture("bus_error_limit.json"), s));
    TEST_ASSERT_EQUAL_INT(-1, bus("{\"bustime-response\":{}}", s));
    TEST_ASSERT_EQUAL_INT(-1, bus("nope", s));
}

static void test_opposite_train_platform() {
    cta::PickedStop o = {};
    TEST_ASSERT_TRUE(cta::oppositeTrainPlatform("Red", "30256", "95th/Dan Ryan", o));
    TEST_ASSERT_EQUAL_STRING("Belmont", o.name);
    TEST_ASSERT_EQUAL_STRING("Howard", o.dir);
    TEST_ASSERT_EQUAL_STRING("Red", o.route);
    TEST_ASSERT_TRUE(strcmp(o.id, "30256") != 0 && o.id[0] == '3');
    TEST_ASSERT_EQUAL_INT((int)cta::Kind::Train, (int)o.type);
    cta::PickedStop back = {};   // and back again
    TEST_ASSERT_TRUE(cta::oppositeTrainPlatform("Red", o.id, o.dir, back));
    TEST_ASSERT_EQUAL_STRING("30256", back.id);
    TEST_ASSERT_TRUE(cta::oppositeTrainPlatform("Brn", "30104", "Loop", o));   // Diversey, shared with Purple
    TEST_ASSERT_EQUAL_STRING("Diversey", o.name);
    TEST_ASSERT_EQUAL_STRING("Kimball", o.dir);
    TEST_ASSERT_FALSE(cta::oppositeTrainPlatform("Red", "99999", "Howard", o));
    TEST_ASSERT_FALSE(cta::oppositeTrainPlatform("Blue", "30256", "Forest Park", o));   // not a Blue station
}

static void test_opposite_bus_stop() {
    // Two patterns: southbound through 1828, northbound with a stop across the street (about 30 m) and one 400 m on.
    const std::string j =
        "{\"bustime-response\":{\"ptr\":["
        "{\"pid\":1,\"rtdir\":\"Southbound\",\"pt\":["
        "{\"typ\":\"S\",\"stpid\":\"1827\",\"stpnm\":\"Clark & Roscoe\",\"lat\":41.9436,\"lon\":-87.6496},"
        "{\"typ\":\"W\",\"lat\":41.9420,\"lon\":-87.6490},"
        "{\"typ\":\"S\",\"stpid\":\"1828\",\"stpnm\":\"Clark & Belmont\",\"lat\":41.9399,\"lon\":-87.6482}]},"
        "{\"pid\":2,\"rtdir\":\"Northbound\",\"pt\":["
        "{\"typ\":\"S\",\"stpid\":\"1913\",\"stpnm\":\"Clark & Barry\",\"lat\":41.9375,\"lon\":-87.6470},"
        "{\"typ\":\"S\",\"stpid\":\"1914\",\"stpnm\":\"Clark & Belmont\",\"lat\":41.9401,\"lon\":-87.6484}]}]}}";
    cta::PickedStop o = {};
    snprintf(o.routeName, sizeof(o.routeName), "Clark");
    TEST_ASSERT_TRUE(cta::oppositeBusStop(j.c_str(), j.size(), "22", "1828", "Southbound", o));
    TEST_ASSERT_EQUAL_STRING("1914", o.id);
    TEST_ASSERT_EQUAL_STRING("Clark & Belmont", o.name);
    TEST_ASSERT_EQUAL_STRING("Northbound", o.dir);
    TEST_ASSERT_EQUAL_STRING("22", o.route);
    TEST_ASSERT_EQUAL_STRING("Clark", o.routeName);   // kept
    TEST_ASSERT_EQUAL_INT((int)cta::Kind::Bus, (int)o.type);
    TEST_ASSERT_FALSE(cta::oppositeBusStop(j.c_str(), j.size(), "22", "9999", "Southbound", o));
    // the shared fixture is one way only: nothing the other way
    const std::string f = readFixture("bus_patterns.json");
    TEST_ASSERT_FALSE(cta::oppositeBusStop(f.c_str(), f.size(), "22", "2002", "Southbound", o));
    TEST_ASSERT_FALSE(cta::oppositeBusStop("x", 1, "22", "1828", "Southbound", o));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_train_follow_fixture);
    RUN_TEST(test_train_follow_gone_and_errors);
    RUN_TEST(test_bus_follow_fixture);
    RUN_TEST(test_bus_follow_gone_and_errors);
    RUN_TEST(test_opposite_train_platform);
    RUN_TEST(test_opposite_bus_stop);
    return UNITY_END();
}
