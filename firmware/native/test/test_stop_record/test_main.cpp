// Stop record codec and UTF-8-safe truncation (plan 1, task 3). Mirrors the app's StopRecordTests.
#include <unity.h>

#include <string.h>

#include <string>

#include "cta_config.h"
#include "cta_types.h"
#include "stop_record.h"
#include "text.h"

void setUp() {}
void tearDown() {}

static const char* kS24 = "NNNNNNNNNNNNNNNNNNNNNNNN";  // 24 bytes

static void setArrival(cta::Arrival& a, int16_t min, const char* dest, bool live, bool delayed) {
    a.min = min;
    cta::copyTruncated(a.dest, dest, cta::kNameLen);
    a.live = live;
    a.delayed = delayed;
}

static cta::StopRecord sample() {
    cta::StopRecord r{};
    strcpy(r.id, "30255");
    r.type = cta::Kind::Train;
    strcpy(r.route, "Red");
    strcpy(r.dir, "95th/Dan Ryan");
    strcpy(r.name, "Belmont");
    setArrival(r.arrivals[0], 3, "95th/Dan Ryan", true, false);
    setArrival(r.arrivals[1], 12, "Howard", false, true);
    r.count = 2;
    r.alert = true;
    r.ts = 1763000000;
    return r;
}

static cta::StopRecord worst() {
    cta::StopRecord r{};
    strcpy(r.id, "4132099");
    r.type = cta::Kind::Train;
    strcpy(r.route, "1234567");
    strcpy(r.dir, kS24);
    strcpy(r.name, kS24);
    for (size_t i = 0; i < cta::kMaxArrivals; ++i) setArrival(r.arrivals[i], 999, kS24, false, false);
    r.count = cta::kMaxArrivals;
    r.ts = 4294967295u;
    return r;
}

static void assertSame(const cta::StopRecord& a, const cta::StopRecord& b) {
    TEST_ASSERT_EQUAL_STRING(a.id, b.id);
    TEST_ASSERT_EQUAL_INT((int)a.type, (int)b.type);
    TEST_ASSERT_EQUAL_STRING(a.route, b.route);
    TEST_ASSERT_EQUAL_STRING(a.dir, b.dir);
    TEST_ASSERT_EQUAL_STRING(a.name, b.name);
    TEST_ASSERT_EQUAL_UINT8(a.count, b.count);
    for (uint8_t i = 0; i < a.count; ++i) {
        TEST_ASSERT_EQUAL_INT16(a.arrivals[i].min, b.arrivals[i].min);
        TEST_ASSERT_EQUAL_STRING(a.arrivals[i].dest, b.arrivals[i].dest);
        TEST_ASSERT_EQUAL(a.arrivals[i].live, b.arrivals[i].live);
        TEST_ASSERT_EQUAL(a.arrivals[i].delayed, b.arrivals[i].delayed);
    }
    TEST_ASSERT_EQUAL(a.alert, b.alert);
    TEST_ASSERT_EQUAL_UINT32(a.ts, b.ts);
}

static void test_round_trip() {
    const cta::StopRecord r = sample();
    char buf[cta::kBleMaxPayload + 1];
    const size_t n = cta::encodeStopRecord(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_NULL(strstr(buf, "\\/"));  // no escaped slashes wasting bytes
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"train\""));
    cta::StopRecord d{};
    TEST_ASSERT_TRUE(cta::decodeStopRecord(buf, n, d));
    assertSame(r, d);
}

static void test_round_trip_bus_and_empty() {
    cta::StopRecord r{};
    strcpy(r.id, "1001");
    r.type = cta::Kind::Bus;
    strcpy(r.route, "22");
    strcpy(r.dir, "Southbound");
    strcpy(r.name, "Clark & Belmont");
    r.count = 0;
    char buf[cta::kBleMaxPayload + 1];
    const size_t n = cta::encodeStopRecord(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"bus\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"arrivals\":[]"));
    cta::StopRecord d{};
    TEST_ASSERT_TRUE(cta::decodeStopRecord(buf, n, d));
    assertSame(r, d);
}

static void test_encode_shape() {
    cta::StopRecord r{};
    strcpy(r.id, "1");
    r.type = cta::Kind::Bus;
    setArrival(r.arrivals[0], 4, "Harrison", true, true);
    r.count = 1;
    r.ts = 7;
    char buf[cta::kBleMaxPayload + 1];
    TEST_ASSERT_TRUE(cta::encodeStopRecord(r, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"1\",\"type\":\"bus\",\"route\":\"\",\"dir\":\"\",\"name\":\"\",\"arrivals\":[{\"min\":4,"
        "\"dest\":\"Harrison\",\"live\":true,\"delayed\":true}],\"alert\":false,\"ts\":7}",
        buf);
}

static void test_truncation() {
    char out[cta::kNameLen + 1];
    cta::copyTruncated(out, "Harlem/Lake (via Marketplace)", 24);
    TEST_ASSERT_EQUAL_STRING("Harlem/Lake (via Marketp", out);
    cta::copyTruncated(out, "AAAAAAAAAAAAAAAAAAAAAAA\xC3\xA9", 24);  // é would straddle byte 24
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAAAAAAAAA", out);
    cta::copyTruncated(out, "Belmont", 24);
    TEST_ASSERT_EQUAL_STRING("Belmont", out);
    cta::copyTruncated(out, "\xC3\xA9\xC3\xA9\xC3\xA9", 4);  // ééé in 4 bytes → éé
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9\xC3\xA9", out);
    cta::copyTruncated(out, kS24, 24);  // exactly at the limit
    TEST_ASSERT_EQUAL_STRING(kS24, out);
    cta::copyTruncated(out, "AAAAAAAAAAAAAAAAAAAAAA\xF0\x9F\x9A\x86", 24);  // a 4-byte 🚆 straddling byte 24
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAAAAAAAA", out);
    cta::copyTruncated(out, "AAAAAAAAAAAAAAAAAAAA\xF0\x9F\x9A\x86", 24);  // ending exactly on it
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAAAAAA\xF0\x9F\x9A\x86", out);
}

static void test_truncation_edges() {
    char out[8];
    memset(out, 'x', sizeof(out));
    cta::copyTruncated(out, "", 7);
    TEST_ASSERT_EQUAL_STRING("", out);
    memset(out, 'x', sizeof(out));
    cta::copyTruncated(out, nullptr, 7);
    TEST_ASSERT_EQUAL_STRING("", out);
    memset(out, 'x', sizeof(out));
    cta::copyTruncated(out, "abc", 0);
    TEST_ASSERT_EQUAL_STRING("", out);
    // writes at most maxBytes + 1 bytes
    char guard[12];
    memset(guard, 'x', sizeof(guard));
    cta::copyTruncated(guard, "abcdefghijkl", 7);
    TEST_ASSERT_EQUAL_STRING("abcdefg", guard);
    TEST_ASSERT_EQUAL_CHAR('x', guard[8]);
}

static void test_worst_case_fits() {
    const cta::StopRecord r = worst();
    char buf[cta::kBleMaxPayload + 1];
    const size_t n = cta::encodeStopRecord(r, buf, cta::kBleMaxPayload);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n <= cta::kBleMaxPayload);
    TEST_ASSERT_EQUAL_size_t(0, cta::encodeStopRecord(r, buf, 100));
    cta::StopRecord d{};
    TEST_ASSERT_TRUE(cta::decodeStopRecord(buf, n, d));
    assertSame(r, d);
}

static void test_encode_respects_cap_exactly() {
    const cta::StopRecord r = sample();
    char buf[600];
    const size_t n = cta::encodeStopRecord(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    const std::string full(buf, n);
    // cap == length: fits, with no room for a NUL, and nothing written past cap
    char exact[600];
    memset(exact, '#', sizeof(exact));
    TEST_ASSERT_EQUAL_size_t(n, cta::encodeStopRecord(r, exact, n));
    TEST_ASSERT_EQUAL_STRING(full.c_str(), std::string(exact, n).c_str());
    TEST_ASSERT_EQUAL_CHAR('#', exact[n]);
    // one byte short: doesn't fit, and nothing written past cap
    memset(exact, '#', sizeof(exact));
    TEST_ASSERT_EQUAL_size_t(0, cta::encodeStopRecord(r, exact, n - 1));
    TEST_ASSERT_EQUAL_CHAR('#', exact[n - 1]);
    TEST_ASSERT_EQUAL_size_t(0, cta::encodeStopRecord(r, nullptr, 0));
}

static void test_over_limit_does_not_encode() {
    cta::StopRecord r = worst();
    const char* q24 = "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";  // each escapes to 2 bytes
    TEST_ASSERT_EQUAL_size_t(24, strlen(q24));
    strcpy(r.name, q24);
    strcpy(r.dir, q24);
    for (size_t i = 0; i < cta::kMaxArrivals; ++i) strcpy(r.arrivals[i].dest, q24);
    char buf[cta::kBleMaxPayload + 1];
    TEST_ASSERT_EQUAL_size_t(0, cta::encodeStopRecord(r, buf, cta::kBleMaxPayload));
    TEST_ASSERT_TRUE(cta::encodeStopRecord(r, buf, sizeof(buf)) == 0);  // over 512 even with one spare byte
}

static void test_control_characters_stay_valid_json() {
    cta::StopRecord r = sample();
    strcpy(r.name, "Bel\x01mont\tStop\n");
    char buf[cta::kBleMaxPayload + 1];
    const size_t n = cta::encodeStopRecord(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    for (size_t i = 0; i < n; ++i) TEST_ASSERT_TRUE((unsigned char)buf[i] >= 0x20);  // no raw control bytes
    cta::StopRecord d{};
    TEST_ASSERT_TRUE(cta::decodeStopRecord(buf, n, d));
    TEST_ASSERT_EQUAL_STRING("Bel mont Stop ", d.name);
    TEST_ASSERT_EQUAL_STRING("Belmont", sample().name);
}

static void test_decode_keeps_four_and_truncates() {
    std::string json =
        "{\"id\":\"30255999\",\"type\":\"train\",\"route\":\"RedRedRed\",\"dir\":\"\","
        "\"name\":\"Harlem/Lake (via Marketplace)\",\"arrivals\":[";
    for (int i = 0; i < 6; ++i) {
        if (i) json += ",";
        json += "{\"min\":" + std::to_string(i) +
                ",\"dest\":\"Harlem\\/Lake (via Marketplace)\",\"live\":true,\"delayed\":false}";
    }
    json += "],\"alert\":false,\"ts\":1}";
    cta::StopRecord d{};
    TEST_ASSERT_TRUE(cta::decodeStopRecord(json.c_str(), json.size(), d));
    TEST_ASSERT_EQUAL_UINT8(4, d.count);
    TEST_ASSERT_EQUAL_INT16(3, d.arrivals[3].min);
    TEST_ASSERT_EQUAL_STRING("Harlem/Lake (via Marketp", d.name);
    TEST_ASSERT_EQUAL_STRING("Harlem/Lake (via Marketp", d.arrivals[0].dest);  // "\/" from Swift reads as "/"
    TEST_ASSERT_EQUAL_STRING("3025599", d.id);   // ids and routes are cut to 7 bytes, like the app's idLen
    TEST_ASSERT_EQUAL_STRING("RedRedR", d.route);
}

static void test_decode_rejects() {
    cta::StopRecord r = sample();
    TEST_ASSERT_FALSE(cta::decodeStopRecord("{\"id\":", 6, r));
    const char* noId = "{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"\",\"name\":\"\",\"arrivals\":[],\"alert\":false,\"ts\":1}";
    TEST_ASSERT_FALSE(cta::decodeStopRecord(noId, strlen(noId), r));
    const char* badType = "{\"id\":\"1\",\"type\":\"ferry\",\"arrivals\":[]}";
    TEST_ASSERT_FALSE(cta::decodeStopRecord(badType, strlen(badType), r));
    const char* numericId = "{\"id\":30255,\"type\":\"train\",\"arrivals\":[]}";
    TEST_ASSERT_FALSE(cta::decodeStopRecord(numericId, strlen(numericId), r));
    TEST_ASSERT_FALSE(cta::decodeStopRecord("[]", 2, r));
    TEST_ASSERT_FALSE(cta::decodeStopRecord(nullptr, 0, r));
    assertSame(sample(), r);  // untouched on failure
}

static void test_decode_defaults_and_clamps() {
    const char* json =
        "{\"id\":\"1\",\"type\":\"bus\",\"arrivals\":[{\"min\":-5,\"dest\":\"A\",\"live\":true},"
        "{\"min\":99999,\"dest\":\"B\"}]}";
    cta::StopRecord d = sample();
    TEST_ASSERT_TRUE(cta::decodeStopRecord(json, strlen(json), d));
    TEST_ASSERT_EQUAL_STRING("", d.route);
    TEST_ASSERT_EQUAL_STRING("", d.name);
    TEST_ASSERT_FALSE(d.alert);
    TEST_ASSERT_EQUAL_UINT32(0, d.ts);
    TEST_ASSERT_EQUAL_UINT8(2, d.count);
    TEST_ASSERT_EQUAL_INT16(0, d.arrivals[0].min);      // minutes are never negative (spec 6.2)
    TEST_ASSERT_EQUAL_INT16(32767, d.arrivals[1].min);  // and fit int16
    TEST_ASSERT_FALSE(d.arrivals[1].live);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip);
    RUN_TEST(test_round_trip_bus_and_empty);
    RUN_TEST(test_encode_shape);
    RUN_TEST(test_truncation);
    RUN_TEST(test_truncation_edges);
    RUN_TEST(test_worst_case_fits);
    RUN_TEST(test_encode_respects_cap_exactly);
    RUN_TEST(test_over_limit_does_not_encode);
    RUN_TEST(test_control_characters_stay_valid_json);
    RUN_TEST(test_decode_keeps_four_and_truncates);
    RUN_TEST(test_decode_rejects);
    RUN_TEST(test_decode_defaults_and_clamps);
    return UNITY_END();
}
