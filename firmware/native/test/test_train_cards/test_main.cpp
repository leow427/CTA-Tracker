// A station's arrivals split into one card per line and direction (handheld plan, train_cards.h).
#include <unity.h>

#include <string.h>

#include <string>

#include "cta_types.h"
#include "train_cards.h"
#include "../fixture.h"

void setUp() {}
void tearDown() {}

// Belmont, recorded live on 2026-11-09 at 09:11:09: 36 arrivals on 4 platforms, Red, Brown, and Purple.
static void test_belmont_live() {
    const std::string json = readFixture("train_arrivals_belmont_live.json");
    cta::StopRecord cards[8];
    const int n = cta::parseTrainCards(json.c_str(), json.size(), 1000, "41320", cards, 8);
    TEST_ASSERT_EQUAL(6, n);
    // Soonest card first; dir is the platform's label, not the arrival's destination.
    const char* const route[] = {"Brn", "Red", "P", "P", "Red", "Brn"};
    const char* const dir[] = {"Loop", "Howard", "Loop", "Linden", "95th/Dan Ryan", "Kimball"};
    const int first[] = {0, 1, 2, 4, 8, 18};
    const int count[] = {3, 4, 4, 4, 3, 4};
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_STRING("41320", cards[i].id);
        TEST_ASSERT_EQUAL(static_cast<int>(cta::Kind::Train), static_cast<int>(cards[i].type));
        TEST_ASSERT_EQUAL_STRING(route[i], cards[i].route);
        TEST_ASSERT_EQUAL_STRING(dir[i], cards[i].dir);
        TEST_ASSERT_EQUAL_STRING("Belmont", cards[i].name);
        TEST_ASSERT_EQUAL(first[i], cards[i].arrivals[0].min);
        TEST_ASSERT_EQUAL(count[i], cards[i].count);
        TEST_ASSERT_EQUAL_UINT32(1000, cards[i].ts);
        TEST_ASSERT_FALSE(cards[i].alert);
    }
    // Purple toward Loop: 2, 9 (scheduled), 10, 29 (scheduled), soonest first.
    TEST_ASSERT_EQUAL(9, cards[2].arrivals[1].min);
    TEST_ASSERT_FALSE(cards[2].arrivals[1].live);
    TEST_ASSERT_TRUE(cards[2].arrivals[0].live);
    TEST_ASSERT_EQUAL_STRING("Loop", cards[2].arrivals[0].dest);
}

static void test_max_keeps_the_soonest_cards() {
    const std::string json = readFixture("train_arrivals_belmont_live.json");
    cta::StopRecord cards[2];
    TEST_ASSERT_EQUAL(2, cta::parseTrainCards(json.c_str(), json.size(), 1, "41320", cards, 2));
    TEST_ASSERT_EQUAL_STRING("Brn", cards[0].route);
    TEST_ASSERT_EQUAL_STRING("Red", cards[1].route);
    TEST_ASSERT_EQUAL_STRING("Howard", cards[1].dir);
    // The kept cards still get their later arrivals after the cap is reached.
    TEST_ASSERT_EQUAL(3, cards[0].count);
    TEST_ASSERT_EQUAL(4, cards[1].count);
}

// Arrivals out of time order, and a short-turning train whose destination isn't the platform's label.
static void test_order_and_label_beat_response_order() {
    auto eta = [](const char* stp, const char* rt, const char* dest, const char* arrT) {
        return std::string("{\"staNm\":\"Belmont\",\"stpId\":\"") + stp + "\",\"rt\":\"" + rt +
               "\",\"destNm\":\"" + dest + "\",\"arrT\":\"" + arrT + "\",\"isSch\":\"0\",\"isDly\":\"0\"}";
    };
    const std::string json = std::string("{\"ctatt\":{\"tmst\":\"2026-11-09T09:00:00\",\"errCd\":\"0\",\"eta\":[") +
                             eta("30256", "Red", "95th/Dan Ryan", "2026-11-09T09:12:00") + "," +
                             eta("30255", "Red", "Howard", "2026-11-09T09:07:00") + "," +
                             eta("30256", "Red", "Roosevelt", "2026-11-09T09:03:00") + "]}}";
    cta::StopRecord cards[4];
    TEST_ASSERT_EQUAL(2, cta::parseTrainCards(json.c_str(), json.size(), 5, "41320", cards, 4));
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", cards[0].dir);   // the platform's label, not "Roosevelt"
    TEST_ASSERT_EQUAL(2, cards[0].count);
    TEST_ASSERT_EQUAL(3, cards[0].arrivals[0].min);
    TEST_ASSERT_EQUAL_STRING("Roosevelt", cards[0].arrivals[0].dest);
    TEST_ASSERT_EQUAL(12, cards[0].arrivals[1].min);
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", cards[0].arrivals[1].dest);
    TEST_ASSERT_EQUAL_STRING("Howard", cards[1].dir);
    TEST_ASSERT_EQUAL(7, cards[1].arrivals[0].min);
}

static void test_lone_eta_and_no_etas() {
    const std::string one =
        "{\"ctatt\":{\"tmst\":\"2026-11-09T09:00:00\",\"errCd\":\"0\",\"eta\":{\"staNm\":\"Belmont\",\"stpId\":"
        "\"30256\",\"rt\":\"Red\",\"destNm\":\"95th/Dan Ryan\",\"arrT\":\"2026-11-09T09:05:30\",\"isSch\":\"0\","
        "\"isDly\":\"1\"}}}";
    cta::StopRecord cards[4];
    TEST_ASSERT_EQUAL(1, cta::parseTrainCards(one.c_str(), one.size(), 5, "41320", cards, 4));
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", cards[0].dir);
    TEST_ASSERT_EQUAL(5, cards[0].arrivals[0].min);
    TEST_ASSERT_TRUE(cards[0].arrivals[0].delayed);

    const std::string none = "{\"ctatt\":{\"tmst\":\"2026-11-09T09:00:00\",\"errCd\":\"0\",\"eta\":null}}";
    TEST_ASSERT_EQUAL(0, cta::parseTrainCards(none.c_str(), none.size(), 5, "41320", cards, 4));
}

static void test_unknown_platform_falls_back_to_destination() {
    const std::string json =
        "{\"ctatt\":{\"tmst\":\"2026-11-09T09:00:00\",\"errCd\":\"0\",\"eta\":[{\"staNm\":\"Somewhere\",\"stpId\":"
        "\"39999\",\"rt\":\"Red\",\"destNm\":\"Howard\",\"arrT\":\"2026-11-09T09:02:00\",\"isSch\":\"0\","
        "\"isDly\":\"0\"}]}}";
    cta::StopRecord cards[1];
    TEST_ASSERT_EQUAL(1, cta::parseTrainCards(json.c_str(), json.size(), 5, "49999", cards, 1));
    TEST_ASSERT_EQUAL_STRING("Howard", cards[0].dir);
}

static void test_errors() {
    const std::string err = readFixture("train_error.json");
    cta::StopRecord cards[2];
    TEST_ASSERT_EQUAL(-1, cta::parseTrainCards(err.c_str(), err.size(), 1, "41320", cards, 2));
    TEST_ASSERT_EQUAL(-1, cta::parseTrainCards("{not json", 9, 1, "41320", cards, 2));
}

// The guards parseTrainArrivals has too: no tmst with arrivals is an error (so a station's last good cards stay), a
// numeric errCd of 0 is fine, and arrivals at the same second keep response order.
static void test_tmst_errcd_and_ties() {
    const std::string noTmst =
        "{\"ctatt\":{\"errCd\":\"0\",\"eta\":[{\"staNm\":\"Belmont\",\"stpId\":\"30255\",\"rt\":\"Red\","
        "\"destNm\":\"Howard\",\"arrT\":\"2026-11-09T09:05:00\",\"isSch\":\"0\",\"isDly\":\"0\"}]}}";
    cta::StopRecord cards[4];
    TEST_ASSERT_EQUAL(-1, cta::parseTrainCards(noTmst.c_str(), noTmst.size(), 5, "41320", cards, 4));

    auto eta = [](const char* dest) {
        return std::string("{\"staNm\":\"Belmont\",\"stpId\":\"30255\",\"rt\":\"Red\",\"destNm\":\"") + dest +
               "\",\"arrT\":\"2026-11-09T09:05:00\",\"isSch\":\"0\",\"isDly\":\"0\"}";
    };
    const std::string ties = std::string("{\"ctatt\":{\"tmst\":\"2026-11-09T09:00:00\",\"errCd\":0,\"eta\":[") +
                             eta("Howard") + "," + eta("Loyola") + "]}}";
    TEST_ASSERT_EQUAL(1, cta::parseTrainCards(ties.c_str(), ties.size(), 5, "41320", cards, 4));
    TEST_ASSERT_EQUAL(2, cards[0].count);
    TEST_ASSERT_EQUAL_STRING("Howard", cards[0].arrivals[0].dest);
    TEST_ASSERT_EQUAL_STRING("Loyola", cards[0].arrivals[1].dest);
}

static void test_dir_labels() {
    TEST_ASSERT_EQUAL_STRING("Howard", cta::trainDirLabel("Red", "30255"));
    TEST_ASSERT_EQUAL_STRING("Kimball", cta::trainDirLabel("Brn", "30257"));
    TEST_ASSERT_EQUAL_STRING("Linden", cta::trainDirLabel("P", "30257"));
    TEST_ASSERT_NULL(cta::trainDirLabel("Red", "30257"));
    TEST_ASSERT_NULL(cta::trainDirLabel(nullptr, "30257"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_belmont_live);
    RUN_TEST(test_max_keeps_the_soonest_cards);
    RUN_TEST(test_order_and_label_beat_response_order);
    RUN_TEST(test_lone_eta_and_no_etas);
    RUN_TEST(test_unknown_platform_falls_back_to_destination);
    RUN_TEST(test_errors);
    RUN_TEST(test_tmst_errcd_and_ties);
    RUN_TEST(test_dir_labels);
    return UNITY_END();
}
