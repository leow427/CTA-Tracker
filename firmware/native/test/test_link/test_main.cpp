// The link messages (ecosystem spec 4.3, 4.4): the HTTP bodies the app, the dock, and the wearable send each other.
#include <unity.h>

#include <string.h>

#include <string>

#include "link_msg.h"
#include "pet_palette.h"
#include "pet_render.h"
#include "pet_sprites.h"
#include "wifi_list.h"

void setUp() {}
void tearDown() {}

static const char* const kTok = "0123456789abcdef0123456789abcdef";
static const char* const kStop =
    "{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"95th/Dan Ryan\",\"id\":\"41320\",\"name\":\"Belmont\"}";

template <typename T, typename F>
static bool p(const std::string& json, T& out, F parse) { return parse(json.c_str(), json.size(), out); }

static void wifi_accepts_slot_4_rejects_5() {
    cta::WifiMsg w;
    TEST_ASSERT_TRUE(p(std::string("{\"slot\":4,\"ssid\":\"net\",\"pass\":\"password1\"}"), w,
                       [](const char* j, size_t n, cta::WifiMsg& o) { return cta::parseWifi(j, n, 5, o); }));
    TEST_ASSERT_EQUAL_UINT8(4, w.slot);
    TEST_ASSERT_EQUAL_STRING("net", w.ssid);
    TEST_ASSERT_EQUAL_STRING("password1", w.pass);
    const std::string s5 = "{\"slot\":5,\"ssid\":\"net\",\"pass\":\"password1\"}";
    TEST_ASSERT_FALSE(cta::parseWifi(s5.c_str(), s5.size(), 5, w));
    const std::string s1 = "{\"slot\":1,\"ssid\":\"net\",\"pass\":\"password1\"}";
    TEST_ASSERT_FALSE(cta::parseWifi(s1.c_str(), s1.size(), 1, w));   // the dock keeps one
    const std::string neg = "{\"slot\":-1,\"ssid\":\"net\",\"pass\":\"password1\"}";
    TEST_ASSERT_FALSE(cta::parseWifi(neg.c_str(), neg.size(), 5, w));
    TEST_ASSERT_EQUAL_UINT8(4, w.slot);   // untouched
}

static void wifi_rejects_empty_or_long_ssid() {
    cta::WifiMsg w;
    auto msg = [](const std::string& ssid, const std::string& pass) {
        return "{\"slot\":0,\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\"}";
    };
    auto parse = [&](const std::string& j) { return cta::parseWifi(j.c_str(), j.size(), 5, w); };
    TEST_ASSERT_FALSE(parse(msg("", "password1")));
    TEST_ASSERT_FALSE(parse(msg(std::string(33, 'a'), "password1")));
    TEST_ASSERT_TRUE(parse(msg(std::string(32, 'a'), "password1")));
    TEST_ASSERT_EQUAL(32, (int)strlen(w.ssid));
    TEST_ASSERT_TRUE(parse(msg("open", "")));   // an open network
    TEST_ASSERT_TRUE(parse("{\"slot\":2,\"ssid\":\"n\",\"pass\":\"" + std::string(64, 'p') + "\"}"));
    // each rejected body leaves the last good one alone
    for (const std::string& bad : {msg("", "password1"), msg(std::string(33, 'a'), "password1"),
                                   msg("x", std::string(65, 'p')), std::string("{\"slot\":0,\"ssid\":\"x\"}")}) {
        TEST_ASSERT_FALSE_MESSAGE(parse(bad), bad.c_str());
        TEST_ASSERT_EQUAL_UINT8(2, w.slot);
        TEST_ASSERT_EQUAL_STRING("n", w.ssid);
        TEST_ASSERT_EQUAL_STRING(std::string(64, 'p').c_str(), w.pass);
    }
}

static void token_must_be_32_lower_hex() {
    char t[cta::kTokenLen + 1] = "unchanged";
    auto parse = [&](const std::string& tok) {
        const std::string j = "{\"token\":\"" + tok + "\"}";
        return cta::parseToken(j.c_str(), j.size(), t);
    };
    TEST_ASSERT_FALSE(parse(std::string(kTok).substr(0, 31)));
    TEST_ASSERT_FALSE(parse(std::string(kTok) + "0"));
    TEST_ASSERT_FALSE(parse("ABCDEF6789abcdef0123456789abcdef"));
    TEST_ASSERT_FALSE(parse("g123456789abcdef0123456789abcdef"));
    TEST_ASSERT_EQUAL_STRING("unchanged", t);
    TEST_ASSERT_TRUE(parse(kTok));
    TEST_ASSERT_EQUAL_STRING(kTok, t);
}

static void token_matches_bearer_only() {
    const std::string bearer = std::string("Bearer ") + kTok;
    TEST_ASSERT_TRUE(cta::tokenMatches(bearer.c_str(), kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches(kTok, kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches("Bearer ffffffffffffffffffffffffffffffff", kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches((bearer + "0").c_str(), kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches(bearer.substr(0, bearer.size() - 1).c_str(), kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches("Bearer ", ""));   // no token saved: nothing matches
    TEST_ASSERT_FALSE(cta::tokenMatches(bearer.c_str(), ""));
    const std::string zs(32, 'z');   // a stored token that isn't hex never matches, even itself
    TEST_ASSERT_FALSE(cta::tokenMatches(("Bearer " + zs).c_str(), zs.c_str()));
    TEST_ASSERT_FALSE(cta::tokenMatches((std::string("Token: ") + kTok).c_str(), kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches((std::string("bearer ") + kTok).c_str(), kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches(nullptr, kTok));
    TEST_ASSERT_FALSE(cta::tokenMatches(bearer.c_str(), nullptr));
}

static void stop_parses_config_stop_shape() {
    cta::PickedStop s;
    TEST_ASSERT_TRUE(p(std::string(kStop), s, cta::parseStop));
    TEST_ASSERT_EQUAL(cta::Kind::Train, s.type);
    TEST_ASSERT_EQUAL_STRING("Red", s.route);
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", s.dir);
    TEST_ASSERT_EQUAL_STRING("41320", s.id);
    TEST_ASSERT_EQUAL_STRING("Belmont", s.name);
    TEST_ASSERT_TRUE(p(std::string("{\"type\":\"bus\",\"route\":\"22\",\"dir\":\"Southbound\",\"id\":\"1828\","
                                   "\"name\":\"Clark & Belmont\"}"),
                       s, cta::parseStop));
    TEST_ASSERT_EQUAL(cta::Kind::Bus, s.type);
    const char* const bad[] = {
        "{\"type\":\"boat\",\"route\":\"Red\",\"dir\":\"x\",\"id\":\"41320\",\"name\":\"B\"}",
        "{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"x\",\"id\":\"\",\"name\":\"B\"}",
        "{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"x\",\"name\":\"B\"}",
        "{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"x\",\"id\":\"41320123\",\"name\":\"B\"}",   // id too long
        "{\"type\":\"train\",\"route\":\"Redddddd\",\"dir\":\"x\",\"id\":\"41320\",\"name\":\"B\"}",   // route too long
        "[]",
    };
    for (const char* b : bad) {
        TEST_ASSERT_FALSE_MESSAGE(p(std::string(b), s, cta::parseStop), b);
        TEST_ASSERT_EQUAL_STRING("1828", s.id);
    }
}

static void follow_needs_vehicle() {
    std::string j = kStop;
    j.insert(j.size() - 1, ",\"vehicle\":\"812\",\"dest\":\"95th/Dan Ryan\"");
    cta::FollowMsg f;
    TEST_ASSERT_TRUE(p(j, f, cta::parseFollow));
    TEST_ASSERT_EQUAL_STRING("812", f.vehicle);
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", f.dest);
    TEST_ASSERT_EQUAL_STRING("41320", f.stop.id);
    TEST_ASSERT_FALSE(p(std::string(kStop), f, cta::parseFollow));
    std::string longVehicle = kStop;
    longVehicle.insert(longVehicle.size() - 1, ",\"vehicle\":\"12345678\",\"dest\":\"x\"");
    TEST_ASSERT_FALSE(p(longVehicle, f, cta::parseFollow));
    std::string noDest = kStop;
    noDest.insert(noDest.size() - 1, ",\"vehicle\":\"813\"");
    TEST_ASSERT_FALSE(p(noDest, f, cta::parseFollow));
    TEST_ASSERT_EQUAL_STRING("812", f.vehicle);
}

static void long_text_is_cut_to_fit() {
    const std::string longText(60, 'x');
    const std::string stop = "{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"" + longText + "\",\"id\":\"41320\",\"name\":\"" +
                             longText + "\"";
    cta::PickedStop s;
    TEST_ASSERT_TRUE(p(stop + "}", s, cta::parseStop));
    TEST_ASSERT_EQUAL(39, (int)strlen(s.dir));
    TEST_ASSERT_EQUAL(24, (int)strlen(s.name));
    cta::FollowMsg f;
    TEST_ASSERT_TRUE(p(stop + ",\"vehicle\":\"812\",\"dest\":\"" + longText + "\"}", f, cta::parseFollow));
    TEST_ASSERT_EQUAL(24, (int)strlen(f.dest));
    cta::ConfigMsg c;
    TEST_ASSERT_TRUE(p("{\"pet\":{\"name\":\"" + longText + "\",\"colors\":[1,2]}}", c, cta::parseConfig));
    TEST_ASSERT_EQUAL(16, (int)strlen(c.pet.name));
}

static void config_subset_sets_only_has() {
    cta::ConfigMsg c;
    TEST_ASSERT_TRUE(p(std::string("{\"pet\":{\"name\":\"Trackie\",\"colors\":[2976742,8238335]}}"), c,
                       cta::parseConfig));
    TEST_ASSERT_TRUE(c.hasPet);
    TEST_ASSERT_FALSE(c.hasStops);
    TEST_ASSERT_FALSE(c.hasSettings);
    TEST_ASSERT_EQUAL_STRING("Trackie", c.pet.name);
    TEST_ASSERT_EQUAL_HEX32(0x2D6BE6, c.pet.colors[0]);
    TEST_ASSERT_EQUAL_HEX32(0x7DB4FF, c.pet.colors[1]);
}

static const char* const kFullConfig =
    "{\"stops\":[{\"type\":\"train\",\"route\":\"Red\",\"dir\":\"95th/Dan Ryan\",\"id\":\"30256\",\"name\":\"Belmont\"},"
    "{},{\"type\":\"bus\",\"route\":\"22\",\"dir\":\"Southbound\",\"id\":\"1828\",\"name\":\"Clark & Belmont\"}],"
    "\"pet\":{\"name\":\"Trackie\",\"colors\":[2976742,8238335]},"
    "\"settings\":{\"screen\":80,\"led\":40,\"night_start\":60,\"night_end\":300}}";

static void assertSameConfig(const cta::ConfigMsg& a, const cta::ConfigMsg& b) {
    TEST_ASSERT_EQUAL(a.hasStops, b.hasStops);
    TEST_ASSERT_EQUAL(a.hasPet, b.hasPet);
    TEST_ASSERT_EQUAL(a.hasSettings, b.hasSettings);
    for (int i = 0; i < cta::kDockStops; ++i) {
        TEST_ASSERT_TRUE(cta::samePick(a.stops[i], b.stops[i]));
        TEST_ASSERT_EQUAL_STRING(a.stops[i].name, b.stops[i].name);
    }
    TEST_ASSERT_EQUAL_STRING(a.pet.name, b.pet.name);
    TEST_ASSERT_EQUAL_HEX32(a.pet.colors[0], b.pet.colors[0]);
    TEST_ASSERT_EQUAL_HEX32(a.pet.colors[1], b.pet.colors[1]);
    TEST_ASSERT_EQUAL(0, memcmp(&a.settings, &b.settings, sizeof(a.settings)));
}

static void config_settings_keys() {
    cta::ConfigMsg c;
    TEST_ASSERT_TRUE(p(std::string("{\"settings\":{\"screen\":80,\"led\":40,\"night_start\":60,\"night_end\":300}}"), c,
                       cta::parseConfig));
    TEST_ASSERT_TRUE(c.hasSettings);
    TEST_ASSERT_FALSE(c.hasPet);
    TEST_ASSERT_EQUAL(80, c.settings.screen);
    TEST_ASSERT_EQUAL(40, c.settings.led);
    TEST_ASSERT_EQUAL(60, c.settings.nightStart);
    TEST_ASSERT_EQUAL(300, c.settings.nightEnd);
    const char* const bad[] = {
        "{\"settings\":{\"screen\":101,\"led\":40,\"night_start\":60,\"night_end\":300}}",
        "{\"settings\":{\"screen\":80,\"led\":40,\"night_start\":1440,\"night_end\":300}}",
        "{\"settings\":{\"screen\":80,\"led\":40,\"night_start\":60}}",
        "{\"settings\":{\"screen\":80,\"led\":101,\"night_start\":60,\"night_end\":300}}",
        "{\"pet\":{\"name\":\"T\",\"colors\":[1]}}",
        "{\"pet\":{\"name\":\"T\",\"colors\":[1,2,3]}}",
        "{\"pet\":{\"name\":\"T\",\"colors\":[1,16777216]}}",
        "{\"stops\":[{}]}",
        "{\"stops\":\"x\"}",
    };
    TEST_ASSERT_TRUE(p(std::string(kFullConfig), c, cta::parseConfig));   // stops, pet, and settings all set
    const cta::ConfigMsg before = c;
    for (const char* b : bad) {
        TEST_ASSERT_FALSE_MESSAGE(p(std::string(b), c, cta::parseConfig), b);
        assertSameConfig(before, c);
    }
}

static void config_round_trips() {
    const std::string x = kFullConfig;
    cta::ConfigMsg a, b;
    TEST_ASSERT_TRUE(p(x, a, cta::parseConfig));
    TEST_ASSERT_TRUE(a.hasStops && a.hasPet && a.hasSettings);
    TEST_ASSERT_EQUAL_STRING("30256", a.stops[0].id);
    TEST_ASSERT_EQUAL_STRING("", a.stops[1].id);
    char buf[cta::kMaxBody];
    const size_t n = cta::encodeConfig(a, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(cta::parseConfig(buf, n, b));
    assertSameConfig(a, b);
    // only the has* parts
    cta::ConfigMsg petOnly;
    petOnly.hasPet = true;
    petOnly.pet = a.pet;
    TEST_ASSERT_TRUE(cta::encodeConfig(petOnly, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(strstr(buf, "stops"));
    TEST_ASSERT_NULL(strstr(buf, "settings"));
    cta::ConfigMsg settingsOnly;
    settingsOnly.hasSettings = true;
    settingsOnly.settings = a.settings;
    TEST_ASSERT_TRUE(cta::encodeConfig(settingsOnly, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NULL(strstr(buf, "\"pet\""));
    TEST_ASSERT_NULL(strstr(buf, "stops"));
    TEST_ASSERT_EQUAL(0, (int)cta::encodeConfig(a, buf, 10));   // doesn't fit
}

static void bad_json_leaves_state() {
    cta::ConfigMsg c;
    TEST_ASSERT_TRUE(p(std::string("{\"pet\":{\"name\":\"Trackie\",\"colors\":[1,2]}}"), c, cta::parseConfig));
    TEST_ASSERT_FALSE(p(std::string("{oops"), c, cta::parseConfig));
    TEST_ASSERT_TRUE(c.hasPet);
    TEST_ASSERT_EQUAL_STRING("Trackie", c.pet.name);
    cta::PickedStop s;
    TEST_ASSERT_TRUE(p(std::string(kStop), s, cta::parseStop));
    TEST_ASSERT_FALSE(p(std::string("{oops"), s, cta::parseStop));
    TEST_ASSERT_EQUAL_STRING("41320", s.id);
    cta::WifiMsg w;
    const char* ok = "{\"slot\":0,\"ssid\":\"n\",\"pass\":\"p\"}";
    TEST_ASSERT_TRUE(cta::parseWifi(ok, strlen(ok), 1, w));
    TEST_ASSERT_FALSE(cta::parseWifi("{oops", 5, 1, w));
    TEST_ASSERT_EQUAL_STRING("n", w.ssid);
    TEST_ASSERT_FALSE(cta::parseConfig(nullptr, 0, c));
    TEST_ASSERT_FALSE(cta::parseStop(nullptr, 0, s));
}

static void rejects_oversize() {
    // A valid body padded with spaces to kMaxBody + 1 bytes: only its length is wrong.
    auto pad = [](std::string j) {
        j.append(cta::kMaxBody + 1 - j.size(), ' ');
        return j;
    };
    std::string follow = kStop;
    follow.insert(follow.size() - 1, ",\"vehicle\":\"812\",\"dest\":\"x\"");
    const std::string wifi = pad("{\"slot\":0,\"ssid\":\"n\",\"pass\":\"p\"}");
    const std::string tok = pad(std::string("{\"token\":\"") + kTok + "\"}");
    const std::string stop = pad(kStop), fol = pad(follow);
    const std::string cfg = pad("{\"pet\":{\"name\":\"T\",\"colors\":[1,2]}}");
    cta::WifiMsg w;
    char t[cta::kTokenLen + 1];
    cta::PickedStop s;
    cta::FollowMsg f;
    cta::ConfigMsg c;
    TEST_ASSERT_FALSE(cta::parseWifi(wifi.c_str(), wifi.size(), 5, w));
    TEST_ASSERT_FALSE(cta::parseToken(tok.c_str(), tok.size(), t));
    TEST_ASSERT_FALSE(cta::parseStop(stop.c_str(), stop.size(), s));
    TEST_ASSERT_FALSE(cta::parseFollow(fol.c_str(), fol.size(), f));
    TEST_ASSERT_FALSE(cta::parseConfig(cfg.c_str(), cfg.size(), c));
    // one byte less is fine
    TEST_ASSERT_TRUE(cta::parseWifi(wifi.c_str(), cta::kMaxBody, 5, w));
    TEST_ASSERT_TRUE(cta::parseConfig(cfg.c_str(), cta::kMaxBody, c));
}

static void rejects_nesting_past_10() {
    // Ten levels parse; eleven are malformed, so a body of brackets can't recurse deep into the parser's stack.
    // Brackets inside a string don't count.
    std::string stop = kStop;
    stop.insert(stop.size() - 1, ",\"x\":[[[[[[[[[]]]]]]]]]");
    cta::PickedStop s;
    TEST_ASSERT_TRUE(p(stop, s, cta::parseStop));
    stop.insert(stop.size() - 1, ",\"y\":[[[[[[[[[[]]]]]]]]]]");
    TEST_ASSERT_FALSE(p(stop, s, cta::parseStop));
    std::string named = kStop;
    named.replace(named.find("Belmont"), 7, "[[[[[[[[[[[[{{{{");
    TEST_ASSERT_TRUE(p(named, s, cta::parseStop));
    TEST_ASSERT_EQUAL_STRING("[[[[[[[[[[[[{{{{", s.name);
    TEST_ASSERT_FALSE(p(std::string(600, '['), s, cta::parseStop));
}

static void info_has_new_fields() {
    cta::InfoMsg in = {};
    in.dock = false;
    strcpy(in.id, "a1b2");
    strcpy(in.fw, "0.4.0");
    in.battery = 87;
    in.charging = true;
    in.wifiConnected = true;
    strcpy(in.ssid, "net");
    in.lastUpdate = 1700000000;
    in.uptime = 42;
    in.picksVer = 3;
    in.wake = 7;
    in.docked = true;
    char buf[512];
    TEST_ASSERT_TRUE(cta::encodeInfo(in, buf, sizeof(buf)) > 0);
    for (const char* want : {"\"type\":\"handheld\"", "\"id\":\"a1b2\"", "\"fw\":\"0.4.0\"", "\"battery\":87",
                             "\"charging\":true", "\"wifi\":{\"connected\":true,\"ssid\":\"net\"}",
                             "\"last_update\":1700000000", "\"picks_ver\":3", "\"wake\":7", "\"docked\":true",
                             "\"uptime\":42"})
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, want), want);
    in.dock = true;
    in.battery = -1;
    TEST_ASSERT_TRUE(cta::encodeInfo(in, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"dock\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"battery\":null"));
    TEST_ASSERT_EQUAL(0, (int)cta::encodeInfo(in, buf, 20));
}

static cta::WifiList twoSaved() {
    cta::WifiList l = {};
    strcpy(l.nets[0].ssid, "A");
    strcpy(l.nets[3].ssid, "B");
    return l;
}

static void info_has_paired() {
    cta::InfoMsg in = {};
    in.dock = true;
    in.battery = -1;
    char buf[512];
    TEST_ASSERT_TRUE(cta::encodeInfo(in, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"paired\":false"));
    in.paired = true;
    TEST_ASSERT_TRUE(cta::encodeInfo(in, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"paired\":true"));
}

static void strongest_picks_highest_rssi() {
    const cta::WifiList l = twoSaved();
    const cta::SeenNet seen[] = {{"A", -70}, {"other", -30}, {"B", -55}};
    TEST_ASSERT_EQUAL(3, cta::strongestSaved(l, seen, 3, 0));
    const cta::SeenNet tie[] = {{"B", -60}, {"A", -60}};
    TEST_ASSERT_EQUAL(0, cta::strongestSaved(l, tie, 2, 0));   // a tie goes to the lower slot (home first)
}

static void strongest_skips_masked_slot() {
    const cta::WifiList l = twoSaved();
    const cta::SeenNet seen[] = {{"A", -70}, {"B", -55}};
    TEST_ASSERT_EQUAL(0, cta::strongestSaved(l, seen, 2, 1u << 3));
    TEST_ASSERT_EQUAL(-1, cta::strongestSaved(l, seen, 2, (1u << 3) | 1u));
}

static void strongest_none_seen() {
    const cta::WifiList l = twoSaved();
    TEST_ASSERT_EQUAL(-1, cta::strongestSaved(l, nullptr, 0, 0));
    const cta::SeenNet seen[] = {{"", -40}, {nullptr, -40}, {"C", -40}};   // free slots never match ""
    TEST_ASSERT_EQUAL(-1, cta::strongestSaved(l, seen, 3, 0));
}

static void palette_changes_slots_2_3_only() {
    uint16_t pal[pet::kPaletteSize];
    memcpy(pal, pet::kDefaultPalette565, sizeof(pal));
    const uint32_t colors[2] = {0xC60C30, 0x565A5C};
    pet::applyColors(pal, colors);
    TEST_ASSERT_EQUAL_HEX16(pet::rgb565(0xC60C30), pal[2]);
    TEST_ASSERT_EQUAL_HEX16(pet::rgb565(0x565A5C), pal[3]);
    for (size_t i = 0; i < pet::kPaletteSize; ++i)
        if (i != 2 && i != 3) TEST_ASSERT_EQUAL_HEX16(pet::kDefaultPalette565[i], pal[i]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(wifi_accepts_slot_4_rejects_5);
    RUN_TEST(wifi_rejects_empty_or_long_ssid);
    RUN_TEST(token_must_be_32_lower_hex);
    RUN_TEST(token_matches_bearer_only);
    RUN_TEST(stop_parses_config_stop_shape);
    RUN_TEST(follow_needs_vehicle);
    RUN_TEST(long_text_is_cut_to_fit);
    RUN_TEST(config_subset_sets_only_has);
    RUN_TEST(config_settings_keys);
    RUN_TEST(config_round_trips);
    RUN_TEST(bad_json_leaves_state);
    RUN_TEST(rejects_oversize);
    RUN_TEST(rejects_nesting_past_10);
    RUN_TEST(info_has_new_fields);
    RUN_TEST(info_has_paired);
    RUN_TEST(strongest_picks_highest_rssi);
    RUN_TEST(strongest_skips_masked_slot);
    RUN_TEST(strongest_none_seen);
    RUN_TEST(palette_changes_slots_2_3_only);
    return UNITY_END();
}
