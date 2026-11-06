#include <string.h>
#include <unity.h>

#include "stop_record.h"

void setUp() {}
void tearDown() {}

static cta::StopRecord sample() {
    cta::StopRecord r{};
    strcpy(r.id, "30255");
    r.type = cta::Kind::Train;
    strcpy(r.route, "Red");
    strcpy(r.dir, "95th/Dan Ryan");
    strcpy(r.name, "Belmont");
    r.count = 2;
    r.arrivals[0] = {3, "95th/Dan Ryan", true, false};
    r.arrivals[1] = {11, "95th/Dan Ryan", false, true};
    r.alert = true;
    r.ts = 1763000000;
    return r;
}

static void test_round_trip() {
    cta::StopRecord r = sample(), back{};
    char buf[512];
    size_t n = cta::encodeStopRecord(r, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(cta::decodeStopRecord(buf, n, back));
    TEST_ASSERT_EQUAL_STRING(r.id, back.id);
    TEST_ASSERT_EQUAL_STRING(r.route, back.route);
    TEST_ASSERT_EQUAL_STRING(r.dir, back.dir);
    TEST_ASSERT_EQUAL_STRING(r.name, back.name);
    TEST_ASSERT_EQUAL_UINT8(2, back.count);
    TEST_ASSERT_EQUAL_INT16(11, back.arrivals[1].min);
    TEST_ASSERT_FALSE(back.arrivals[1].live);
    TEST_ASSERT_TRUE(back.arrivals[1].delayed);
    TEST_ASSERT_TRUE(back.alert);
    TEST_ASSERT_EQUAL_UINT32(1763000000, back.ts);
}

static void test_worst_case_fits() {
    cta::StopRecord w{};
    strcpy(w.id, "1234567");
    strcpy(w.route, "1234567");
    memset(w.dir, 'D', 24);
    memset(w.name, 'N', 24);
    w.count = 4;
    for (auto& a : w.arrivals) {
        a.min = 999;
        memset(a.dest, 'X', 24);
        a.live = a.delayed = true;
    }
    w.alert = true;
    w.ts = 4294967295u;
    char buf[512];
    TEST_ASSERT_TRUE(cta::encodeStopRecord(w, buf, 512) > 0);
    TEST_ASSERT_EQUAL_size_t(0, cta::encodeStopRecord(w, buf, 100));
    size_t n = cta::encodeStopRecord(w, buf, 512);
    TEST_ASSERT_EQUAL_size_t(n, cta::encodeStopRecord(w, buf, n + 1));   // exact fit, room for the NUL
    TEST_ASSERT_EQUAL_size_t(0, cta::encodeStopRecord(w, buf, n));       // one short: never truncated JSON
}

static void test_decode_limits() {
    const char* six = "{\"id\":\"1001\",\"type\":\"bus\",\"route\":\"22\",\"dir\":\"Southbound\",\"name\":"
                      "\"Clark & Belmont and a very long tail\",\"arrivals\":[{\"min\":1},{\"min\":2},{\"min\":3},"
                      "{\"min\":4},{\"min\":5},{\"min\":6}],\"alert\":false,\"ts\":5}";
    cta::StopRecord r{};
    TEST_ASSERT_TRUE(cta::decodeStopRecord(six, strlen(six), r));
    TEST_ASSERT_EQUAL_UINT8(4, r.count);
    TEST_ASSERT_TRUE(r.type == cta::Kind::Bus);
    TEST_ASSERT_EQUAL_STRING("Clark & Belmont and a ve", r.name);
    TEST_ASSERT_FALSE(cta::decodeStopRecord("{\"id\":", 6, r));
    TEST_ASSERT_FALSE(cta::decodeStopRecord("{\"name\":\"x\"}", 12, r));
    TEST_ASSERT_FALSE(cta::decodeStopRecord("[1]", 3, r));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip);
    RUN_TEST(test_worst_case_fits);
    RUN_TEST(test_decode_limits);
    return UNITY_END();
}
