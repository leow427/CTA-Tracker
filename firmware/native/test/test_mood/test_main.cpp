#include <unity.h>

#include <initializer_list>

#include "mood.h"

using cta::Mood;

void setUp() {}
void tearDown() {}

static const uint32_t kNow = 10000;

static cta::StopRecord rec(uint8_t count, int16_t min0, bool live0, bool delayed0, bool alert, uint32_t ts) {
    cta::StopRecord r{};
    r.count = count;
    r.arrivals[0].min = min0;
    r.arrivals[0].live = live0;
    r.arrivals[0].delayed = delayed0;
    r.alert = alert;
    r.ts = ts;
    return r;
}

static const cta::StopRecord kHappy = rec(1, 8, true, false, false, kNow - 10);
static const cta::StopRecord kWorried = rec(1, 8, true, true, false, kNow - 10);
static const cta::StopRecord kSad = rec(1, 8, true, false, true, kNow - 10);
static const cta::StopRecord kSleepy = rec(1, 8, true, false, false, 0);
static const bool kAll[3] = {true, true, true};

static Mood device(std::initializer_list<cta::StopRecord> list, bool night = false, bool location = true) {
    cta::StopRecord r[3];
    size_t n = 0;
    for (const auto& x : list) r[n++] = x;
    return cta::deviceMood(r, kAll, n, kNow, night, location);
}

static void test_stop_moods() {
    TEST_ASSERT_EQUAL(Mood::Sleepy, cta::stopMood(rec(1, 5, true, false, false, 0), kNow, false));
    TEST_ASSERT_EQUAL(Mood::Sleepy, cta::stopMood(rec(1, 5, true, false, false, kNow - 301), kNow, false));
    TEST_ASSERT_EQUAL(Mood::Happy, cta::stopMood(rec(1, 5, true, false, false, kNow - 300), kNow, false));
    TEST_ASSERT_EQUAL(Mood::Sleepy, cta::stopMood(kHappy, kNow, true));
    TEST_ASSERT_EQUAL(Mood::Sad, cta::stopMood(rec(1, 5, true, false, true, kNow - 10), kNow, false));
    TEST_ASSERT_EQUAL(Mood::Sad, cta::stopMood(rec(0, 0, false, false, false, kNow - 10), kNow, false));
    TEST_ASSERT_EQUAL(Mood::Worried, cta::stopMood(kWorried, kNow, false));
    TEST_ASSERT_EQUAL(Mood::Happy, cta::stopMood(kHappy, kNow, false));
    TEST_ASSERT_EQUAL(Mood::Happy, cta::stopMood(rec(1, 5, false, false, false, kNow - 10), kNow, false));
}

static void test_device_moods() {
    TEST_ASSERT_EQUAL(Mood::Sleepy, device({kHappy, kHappy}, false, false));   // no location
    TEST_ASSERT_EQUAL(Mood::Sleepy, device({kHappy}, true));                   // night
    TEST_ASSERT_EQUAL(Mood::Hyped, device({kHappy, rec(1, 3, true, false, false, kNow - 10)}));
    TEST_ASSERT_EQUAL(Mood::Happy, device({rec(1, 4, true, false, false, kNow - 10)}));
    TEST_ASSERT_EQUAL(Mood::Happy, device({rec(1, 1, false, false, false, kNow - 10)}));   // scheduled can't hype
    TEST_ASSERT_EQUAL(Mood::Sleepy, device({rec(1, 1, true, false, false, kNow - 400)}));   // stale can't hype
    cta::StopRecord far[2] = {kHappy, rec(1, 1, true, false, false, kNow - 10)};
    const bool onlyFirst[2] = {true, false};
    TEST_ASSERT_EQUAL(Mood::Happy, cta::deviceMood(far, onlyFirst, 2, kNow, false, true));   // far stop can't hype
    TEST_ASSERT_EQUAL(Mood::Happy, device({kHappy, kHappy, kWorried}));
    TEST_ASSERT_EQUAL(Mood::Worried, device({kHappy, kWorried}));    // tie → Worried
    TEST_ASSERT_EQUAL(Mood::Sad, device({kSad, kWorried}));          // tie → Sad
    TEST_ASSERT_EQUAL(Mood::Sad, device({kSad, kHappy}));
    TEST_ASSERT_EQUAL(Mood::Worried, device({kSleepy, kSleepy, kWorried}));   // Sleepy stops excluded
    TEST_ASSERT_EQUAL(Mood::Sleepy, device({kSleepy, kSleepy}));
    TEST_ASSERT_EQUAL(Mood::Sleepy, device({}));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_stop_moods);
    RUN_TEST(test_device_moods);
    return UNITY_END();
}
