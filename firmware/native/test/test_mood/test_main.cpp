// Stop and device mood rules (plan 1, task 7; spec 6.3). Mirrors the app's MoodRulesTests.
#include <unity.h>

#include <string.h>

#include "cta_types.h"
#include "mood.h"

using cta::Mood;

void setUp() {}
void tearDown() {}

static const uint32_t kNow = 10000;

static cta::StopRecord rec(uint8_t count, int16_t min0, bool live0, bool delayed0, bool alert, uint32_t ts) {
    cta::StopRecord r{};
    strcpy(r.id, "30255");
    r.type = cta::Kind::Train;
    r.count = count;
    if (count > 0) {
        r.arrivals[0].min = min0;
        r.arrivals[0].live = live0;
        r.arrivals[0].delayed = delayed0;
    }
    r.alert = alert;
    r.ts = ts;
    return r;
}

static cta::StopRecord happy() { return rec(1, 5, true, false, false, 9990); }
static cta::StopRecord worried() { return rec(1, 5, true, true, false, 9990); }
static cta::StopRecord sad() { return rec(1, 5, true, false, true, 9990); }
static cta::StopRecord sleepy() { return rec(1, 5, true, false, false, 0); }

static Mood device(const cta::StopRecord* recs, size_t n, bool eligible = true, bool night = false,
                   bool hasLocation = true) {
    bool elig[8];
    for (size_t i = 0; i < n; ++i) elig[i] = eligible;
    return cta::deviceMood(recs, elig, n, kNow, night, hasLocation);
}

#define EQ(m, expr) TEST_ASSERT_EQUAL_INT((int)Mood::m, (int)(expr))
#define NE(m, expr) TEST_ASSERT_NOT_EQUAL_INT((int)Mood::m, (int)(expr))

static void test_stop_mood() {
    EQ(Sleepy, cta::stopMood(rec(1, 5, true, false, false, 0), kNow, false));          // never fetched
    EQ(Sleepy, cta::stopMood(rec(1, 5, true, false, false, kNow - 301), kNow, false));  // stale
    EQ(Happy, cta::stopMood(rec(1, 5, true, false, false, kNow - 300), kNow, false));   // exactly 5 min: still fresh
    EQ(Sleepy, cta::stopMood(happy(), kNow, true));                                     // night
    EQ(Sad, cta::stopMood(rec(1, 5, true, false, true, 9990), kNow, false));            // alert
    EQ(Sad, cta::stopMood(rec(1, 5, true, true, true, 9990), kNow, false));             // alert beats delay
    EQ(Sad, cta::stopMood(rec(0, 0, false, false, false, 9990), kNow, false));          // no predictions
    EQ(Worried, cta::stopMood(worried(), kNow, false));
    EQ(Happy, cta::stopMood(happy(), kNow, false));
    EQ(Happy, cta::stopMood(rec(1, 5, false, false, false, 9990), kNow, false));        // scheduled only
    EQ(Sleepy, cta::stopMood(rec(0, 0, false, false, true, 0), kNow, false));           // stale beats Sad
}

static void test_stop_mood_clock_ahead() {
    // a relayed snapshot stamped by a phone whose clock runs a little ahead is fresh, not wrapped to stale
    EQ(Happy, cta::stopMood(rec(1, 5, true, false, false, kNow + 30), kNow, false));
    EQ(Happy, cta::stopMood(rec(1, 5, true, false, false, 4000000000u), 3999999990u, false));
}

static void test_device_sleepy_cases() {
    const cta::StopRecord all[3] = {happy(), happy(), happy()};
    EQ(Sleepy, device(all, 3, true, false, false));  // no location
    EQ(Sleepy, device(all, 3, true, true, true));    // night
    EQ(Sleepy, device(all, 0));                      // no stops
    const cta::StopRecord ss[2] = {sleepy(), sleepy()};
    EQ(Sleepy, device(ss, 2));
}

static void test_device_hyped() {
    cta::StopRecord r[2] = {rec(1, 3, true, false, false, 9990), happy()};
    EQ(Hyped, device(r, 2));
    r[0] = rec(1, 4, true, false, false, 9990);
    NE(Hyped, device(r, 2));
    r[0] = rec(1, 1, true, false, false, 9990);
    NE(Hyped, device(r, 2, false));  // a far stop can't hype
    r[0] = rec(1, 1, true, false, false, kNow - 301);
    NE(Hyped, device(r, 2));  // Decision 4: stale data can't hype
    r[0] = rec(1, 1, false, false, false, 9990);
    NE(Hyped, device(r, 2));  // scheduled only can't hype
    r[0] = rec(1, 0, true, true, true, 9990);
    EQ(Hyped, device(r, 2));  // hype outranks the stop's own Sad
    r[0] = rec(0, 0, true, false, false, 9990);
    NE(Hyped, device(r, 2));  // no arrivals: nothing to read
    // only the eligible stop counts
    const cta::StopRecord mixed[2] = {rec(1, 1, true, false, false, 9990), rec(1, 9, true, false, false, 9990)};
    const bool elig[2] = {false, true};
    NE(Hyped, cta::deviceMood(mixed, elig, 2, kNow, false, true));
    const bool elig2[2] = {true, false};
    EQ(Hyped, cta::deviceMood(mixed, elig2, 2, kNow, false, true));
}

static void test_device_majority() {
    const cta::StopRecord a[3] = {happy(), happy(), worried()};
    EQ(Happy, device(a, 3));
    const cta::StopRecord b[2] = {happy(), worried()};
    EQ(Worried, device(b, 2));  // tie → Worried
    const cta::StopRecord c[2] = {sad(), worried()};
    EQ(Sad, device(c, 2));  // tie → Sad
    const cta::StopRecord d[3] = {sleepy(), sleepy(), worried()};
    EQ(Worried, device(d, 3));  // Sleepy stops excluded
    const cta::StopRecord e[3] = {sad(), happy(), happy()};
    EQ(Happy, device(e, 3));
    const cta::StopRecord f[3] = {sad(), worried(), happy()};
    EQ(Sad, device(f, 3));  // three-way tie → Sad
    const cta::StopRecord g[4] = {sad(), happy(), happy(), worried()};
    EQ(Happy, device(g, 4));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stop_mood);
    RUN_TEST(test_stop_mood_clock_ahead);
    RUN_TEST(test_device_sleepy_cases);
    RUN_TEST(test_device_hyped);
    RUN_TEST(test_device_majority);
    return UNITY_END();
}
