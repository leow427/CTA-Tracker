// The `nearby` message (spec 8.3, 9): what the handheld accepts as its stop list.
#include <unity.h>

#include <math.h>
#include <string.h>

#include <string>

#include "nearby_msg.h"
#include "../fixture.h"

void setUp() {}
void tearDown() {}

static bool parse(const std::string& json, cta::Nearby& out) { return cta::parseNearby(json.c_str(), json.size(), out); }

static void test_belmont() {
    cta::Nearby n;
    TEST_ASSERT_TRUE(parse(readFixture("nearby_belmont.json"), n));
    TEST_ASSERT_EQUAL(10, n.nTrains);
    TEST_ASSERT_EQUAL(10, n.nBuses);
    TEST_ASSERT_EQUAL(3, n.nClose);
    TEST_ASSERT_EQUAL_STRING("41320", n.trains[0]);
    TEST_ASSERT_EQUAL_STRING("40660", n.trains[9]);
    TEST_ASSERT_EQUAL_STRING("14915", n.buses[0]);
    TEST_ASSERT_EQUAL_STRING("14437", n.close[2]);
    TEST_ASSERT_TRUE(fabs(n.lat - 41.939) < 1e-9);
    TEST_ASSERT_TRUE(fabs(n.lon + 87.651) < 1e-9);
}

static void test_empty_lists_are_fine() {
    cta::Nearby n;
    TEST_ASSERT_TRUE(parse("{\"lat\":41.9,\"lon\":-87.6,\"trains\":[],\"buses\":[\"1\"],\"close\":[\"1\"]}", n));
    TEST_ASSERT_EQUAL(0, n.nTrains);
    TEST_ASSERT_EQUAL(1, n.nBuses);
    TEST_ASSERT_TRUE(parse("{\"lat\":0,\"lon\":0,\"trains\":[],\"buses\":[],\"close\":[]}", n));
    TEST_ASSERT_EQUAL(0, n.nBuses);
}

static void test_rejects_and_keeps_the_old_list() {
    cta::Nearby n;
    TEST_ASSERT_TRUE(parse(readFixture("nearby_belmont.json"), n));
    const char* const bad[] = {
        "{not json",
        "{\"lon\":-87.6,\"trains\":[],\"buses\":[],\"close\":[]}",                       // no lat
        "{\"lat\":\"41.9\",\"lon\":-87.6,\"trains\":[],\"buses\":[],\"close\":[]}",      // lat as text
        "{\"lat\":91,\"lon\":-87.6,\"trains\":[],\"buses\":[],\"close\":[]}",            // out of range
        "{\"lat\":41.9,\"lon\":-87.6,\"buses\":[],\"close\":[]}",                        // no trains
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[\"30256\"],\"buses\":[],\"close\":[]}", // a platform, not a station
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[41320],\"buses\":[],\"close\":[]}",     // a number, not a string
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[],\"buses\":[\"12a\"],\"close\":[]}",   // not all digits
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[],\"buses\":[\"1234567890\"],\"close\":[]}",   // too long
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[],\"buses\":[\"1\",\"1\"],\"close\":[]}",      // repeated
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[],\"buses\":[\"1\"],\"close\":[\"2\"]}",       // close not listed
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[],\"buses\":[\"1\",\"2\",\"3\",\"4\"],\"close\":[\"1\",\"2\",\"3\",\"4\"]}",
        "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[],\"buses\":[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\",\"10\","
        "\"11\"],\"close\":[]}",                                                         // 11 stops
    };
    for (const char* b : bad) {
        TEST_ASSERT_FALSE_MESSAGE(parse(b, n), b);
        TEST_ASSERT_EQUAL_STRING("41320", n.trains[0]);   // untouched
        TEST_ASSERT_EQUAL(10, n.nBuses);
    }
    TEST_ASSERT_FALSE(cta::parseNearby(nullptr, 0, n));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_belmont);
    RUN_TEST(test_empty_lists_are_fine);
    RUN_TEST(test_rejects_and_keeps_the_old_list);
    return UNITY_END();
}
