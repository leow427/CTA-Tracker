#include <string.h>
#include <unity.h>

#include <string>

#include "device_config.h"

void setUp() {}
void tearDown() {}

static cta::DeviceConfig picked() {
    cta::DeviceConfig c = cta::defaultConfig();
    const char* j = "{\"stops\":[{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"95th/Dan Ryan\",\"id\":\"30255\","
                    "\"name\":\"Belmont\"},{},{\"type\":\"bus\",\"route\":\"22\",\"dir\":\"Southbound\",\"id\":\"1001\","
                    "\"name\":\"Clark & Belmont\"}]}";
    TEST_ASSERT_TRUE(cta::applyConfigJson(j, strlen(j), c));
    return c;
}

static void test_defaults() {
    cta::DeviceConfig c = cta::defaultConfig();
    TEST_ASSERT_EQUAL_STRING("Trackie", c.petName);
    TEST_ASSERT_EQUAL_INT16(60, c.nightStart);
    TEST_ASSERT_EQUAL_INT16(300, c.nightEnd);
    TEST_ASSERT_EQUAL_CHAR('\0', c.stops[0].id[0]);
}

static void test_partial_write_keeps_stops() {
    cta::DeviceConfig c = picked();
    TEST_ASSERT_EQUAL_STRING("30255", c.stops[0].id);
    TEST_ASSERT_EQUAL_CHAR('\0', c.stops[1].id[0]);
    TEST_ASSERT_TRUE(c.stops[2].type == cta::Kind::Bus);
    const char* j = "{\"pet\":{\"name\":\"Rosie\"}}";
    TEST_ASSERT_TRUE(cta::applyConfigJson(j, strlen(j), c));
    TEST_ASSERT_EQUAL_STRING("Rosie", c.petName);
    TEST_ASSERT_EQUAL_STRING("30255", c.stops[0].id);
    TEST_ASSERT_EQUAL_HEX32(0x2D6BE6, c.petColors[0]);
    const char* s = "{\"settings\":{\"led\":20}}";
    TEST_ASSERT_TRUE(cta::applyConfigJson(s, strlen(s), c));
    TEST_ASSERT_EQUAL_UINT8(20, c.led);
    TEST_ASSERT_EQUAL_UINT8(100, c.screen);
}

static void test_rejects_bad_values_atomically() {
    cta::DeviceConfig c = picked();
    const char* bad[] = {
        "{\"settings\":{\"screen\":101}}",
        "{\"settings\":{\"led\":-1}}",
        "{\"settings\":{\"night_start\":1440}}",
        "{\"pet\":{\"name\":\"Ok\",\"colors\":[1]}}",
        "{\"pet\":{\"colors\":[16777216,0]}}",
        "{\"stops\":[{\"type\":\"tram\",\"route\":\"Red\",\"id\":\"30255\"}]}",
        "{\"stops\":[{\"type\":\"bus\",\"route\":\"22\",\"id\":\"123456789\"}]}",
        "{\"stops\":[{},{},{},{}]}",
        "{\"stops\":{}}",
        "not json",
        "[]",
    };
    for (const char* j : bad) {
        TEST_ASSERT_FALSE_MESSAGE(cta::applyConfigJson(j, strlen(j), c), j);
        TEST_ASSERT_EQUAL_STRING("Trackie", c.petName);
        TEST_ASSERT_EQUAL_STRING("30255", c.stops[0].id);
        TEST_ASSERT_EQUAL_UINT8(100, c.screen);
    }
}

static void test_config_round_trip() {
    cta::DeviceConfig c = picked(), back = cta::defaultConfig();
    strcpy(c.petName, "Rosie");
    c.petColors[0] = 0xC60C30;
    c.led = 33;
    c.nightStart = 23 * 60;
    char buf[512];
    size_t n = cta::encodeConfigJson(c, true, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(cta::applyConfigJson(buf, n, back));
    TEST_ASSERT_EQUAL_MEMORY(&c.stops, &back.stops, sizeof c.stops);
    TEST_ASSERT_EQUAL_STRING("Rosie", back.petName);
    TEST_ASSERT_EQUAL_HEX32(0xC60C30, back.petColors[0]);
    TEST_ASSERT_EQUAL_UINT8(33, back.led);
    TEST_ASSERT_EQUAL_INT16(23 * 60, back.nightStart);
    n = cta::encodeConfigJson(cta::defaultConfig(), true, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"type\":\"train\",\"route\":\"\",\"dir\":\"\",\"id\":\"\",\"name\":\"\"}"));   // empty slot, full shape
    n = cta::encodeConfigJson(c, false, buf, sizeof buf);
    TEST_ASSERT_NULL(strstr(buf, "stops"));
}

static void test_full_config_fits_one_write() {
    cta::DeviceConfig c = cta::defaultConfig();
    for (auto& s : c.stops) {
        s.type = cta::Kind::Bus;
        memset(s.route, '9', 7);
        memset(s.id, '1', 7);
        memset(s.dir, 'D', 24);
        memset(s.name, 'N', 24);
    }
    memset(c.petName, 'P', 16);
    char buf[512];
    TEST_ASSERT_TRUE(cta::encodeConfigJson(c, true, buf, sizeof buf) > 0);
}

static void test_info_and_wifi() {
    char buf[512];
    size_t n = cta::encodeInfoJson("dock", "0.1.0", -1, false, true, "Home", 1763000000, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"battery\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"wifi\":{\"connected\":true,\"ssid\":\"Home\"}"));
    uint8_t slot;
    char ssid[33], pass[64];
    const char* ok = "{\"slot\":0,\"ssid\":\"Home\",\"pass\":\"hunter22\"}";
    TEST_ASSERT_TRUE(cta::decodeWifiJson(ok, strlen(ok), slot, ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("Home", ssid);
    TEST_ASSERT_EQUAL_STRING("hunter22", pass);
    const char* open = "{\"slot\":2,\"ssid\":\"Cafe\"}";
    TEST_ASSERT_TRUE(cta::decodeWifiJson(open, strlen(open), slot, ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("", pass);
    std::string longPass = "{\"slot\":0,\"ssid\":\"Home\",\"pass\":\"" + std::string(64, 'x') + "\"}";
    TEST_ASSERT_FALSE(cta::decodeWifiJson(longPass.c_str(), longPass.size(), slot, ssid, sizeof ssid, pass, sizeof pass));
    std::string longSsid = "{\"slot\":0,\"ssid\":\"" + std::string(33, 's') + "\"}";
    TEST_ASSERT_FALSE(cta::decodeWifiJson(longSsid.c_str(), longSsid.size(), slot, ssid, sizeof ssid, pass, sizeof pass));
    const char* bad[] = {"{\"slot\":3,\"ssid\":\"x\"}", "{\"slot\":-1,\"ssid\":\"x\"}", "{\"ssid\":\"x\"}",
                         "{\"slot\":0,\"ssid\":\"\"}", "{\"slot\":0}", "nope"};
    for (const char* j : bad) TEST_ASSERT_FALSE_MESSAGE(cta::decodeWifiJson(j, strlen(j), slot, ssid, sizeof ssid, pass, sizeof pass), j);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_partial_write_keeps_stops);
    RUN_TEST(test_rejects_bad_values_atomically);
    RUN_TEST(test_config_round_trip);
    RUN_TEST(test_full_config_fits_one_write);
    RUN_TEST(test_info_and_wifi);
    return UNITY_END();
}
