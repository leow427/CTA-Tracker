// Following one vehicle on the wearable's Home card (ecosystem spec 5.7).
#include <unity.h>

#include <string.h>

#include <string>

#include "cta_config.h"
#include "cta_types.h"
#include "follow.h"
#include "link_msg.h"
#include "text.h"
#include "train_cards.h"
#include "../fixture.h"

void setUp() {}
void tearDown() {}

using cta::FollowState;

static cta::FollowMsg follow(const char* dir = "95th/Dan Ryan", const char* vehicle = "812", const char* route = "Red") {
    cta::FollowMsg f{};
    f.stop.type = cta::Kind::Train;
    strcpy(f.stop.route, route);
    strcpy(f.stop.dir, dir);
    strcpy(f.stop.id, "41320");
    strcpy(f.stop.name, "Belmont");
    strcpy(f.vehicle, vehicle);
    strcpy(f.dest, "95th/Dan Ryan");
    return f;
}

// A Red card at Belmont; vehicle "" leaves 812 out (another train, 811, is always there at 2 min).
static cta::StopRecord card(uint32_t ts, int16_t min, const char* vehicle = "812", const char* dir = "95th/Dan Ryan") {
    cta::StopRecord r{};
    strcpy(r.id, "41320");
    r.type = cta::Kind::Train;
    strcpy(r.route, "Red");
    cta::copyTruncated(r.dir, dir, cta::kNameLen);
    strcpy(r.name, "Belmont");
    r.arrivals[0].min = 2;
    strcpy(r.arrivals[0].vehicle, "811");
    r.count = 1;
    if (vehicle[0] != '\0') {
        r.arrivals[1].min = min;
        strcpy(r.arrivals[1].vehicle, vehicle);
        r.count = 2;
    }
    r.ts = ts;
    return r;
}

static cta::FollowView view(const cta::StopRecord& r, cta::FollowMemo& m, uint32_t now) {
    return cta::followView(&r, 1, follow(), m, now);
}

#define ASSERT_VIEW(st, mn, v)                                     \
    do {                                                          \
        const cta::FollowView v_ = (v);                           \
        TEST_ASSERT_EQUAL_INT((int)(st), (int)v_.state);          \
        TEST_ASSERT_EQUAL_INT16((mn), v_.min);                    \
    } while (0)

static void follow_waiting_4_min() {
    cta::FollowMemo m;
    ASSERT_VIEW(FollowState::Waiting, 4, view(card(100, 4), m, 100));
}

static void follow_due_at_0() {
    cta::FollowMemo m;
    ASSERT_VIEW(FollowState::Due, 0, view(card(100, 0), m, 100));
}

static void follow_gone_after_min_1_is_arrived() {
    cta::FollowMemo m;
    ASSERT_VIEW(FollowState::Waiting, 1, view(card(100, 1), m, 100));   // 1 min isn't Due yet
    ASSERT_VIEW(FollowState::Arrived, 1, view(card(130, 0, ""), m, 130));   // gone at an expected 1 min
    ASSERT_VIEW(FollowState::Arrived, 1, view(card(220, 3), m, 220));   // ended stays ended
}

static void follow_gone_at_min_9_is_lost() {
    cta::FollowMemo m;
    view(card(100, 9), m, 100);
    ASSERT_VIEW(FollowState::Waiting, 9, view(card(100, 0, ""), m, 130));   // not newer: proves nothing
    ASSERT_VIEW(FollowState::Lost, 9, view(card(160, 0, ""), m, 160));
}

static void follow_failed_poll_keeps_waiting() {
    cta::FollowMemo m;
    view(card(100, 4), m, 100);
    // The poll failed, so the card keeps its old record (same ts): still Waiting with the last minutes.
    ASSERT_VIEW(FollowState::Waiting, 4, view(card(100, 4), m, 190));
    // No card for the stop at all (a new nearby list dropped it): the same, for now.
    ASSERT_VIEW(FollowState::Waiting, 4, cta::followView(nullptr, 0, follow(), m, 250));
    TEST_ASSERT_FALSE(cta::followExpired(m, 250));
}

static void follow_card_gone_ends_arrived() {   // (a)
    cta::FollowMemo m;
    view(card(100, 4), m, 100);
    const uint32_t due = 100 + (4 + cta::kFollowGraceMin) * 60;
    ASSERT_VIEW(FollowState::Waiting, 4, cta::followView(nullptr, 0, follow(), m, due));
    ASSERT_VIEW(FollowState::Arrived, 4, cta::followView(nullptr, 0, follow(), m, due + 1));
    // Seen at Due: it clears a few minutes later even if every poll then fails.
    cta::FollowMemo d;
    view(card(100, 0), d, 100);
    ASSERT_VIEW(FollowState::Due, 0, view(card(100, 0), d, 100 + cta::kFollowGraceMin * 60));
    ASSERT_VIEW(FollowState::Arrived, 0, view(card(100, 0), d, 101 + cta::kFollowGraceMin * 60));
}

static void follow_never_seen_ends_lost() {   // (b)
    cta::FollowMemo m;
    // Further out than the card's 4 arrivals (or a short-turn): not on the card yet, so Waiting.
    ASSERT_VIEW(FollowState::Waiting, -1, view(card(100, 0, ""), m, 100));
    // Failed polls never give Lost, however long.
    ASSERT_VIEW(FollowState::Waiting, -1, view(card(100, 0, ""), m, 100000));
    ASSERT_VIEW(FollowState::Waiting, -1, view(card(160, 0, ""), m, 100100));
    const uint32_t cap = 100 + cta::kFollowUnseenCapMin * 60;
    ASSERT_VIEW(FollowState::Waiting, -1, view(card(cap - 1, 0, ""), m, 100200));
    ASSERT_VIEW(FollowState::Lost, -1, view(card(cap, 0, ""), m, 100300));
}

static void follow_records_without_ids() {   // (c)
    cta::StopRecord noIds = card(300, 0, "");
    noIds.arrivals[0].vehicle[0] = '\0';
    // Seen once: a record without vehicle IDs can't tell, then the time runs out.
    cta::FollowMemo m;
    view(card(100, 4), m, 100);
    ASSERT_VIEW(FollowState::Waiting, 4, view(noIds, m, 300));
    noIds.ts = 470;
    ASSERT_VIEW(FollowState::Arrived, 4, view(noIds, m, 470));
    // Never seen: Lost after the cap of fresh records.
    cta::FollowMemo u;
    noIds.ts = 100;
    ASSERT_VIEW(FollowState::Waiting, -1, view(noIds, u, 100));
    noIds.ts = 100 + cta::kFollowUnseenCapMin * 60;
    ASSERT_VIEW(FollowState::Lost, -1, view(noIds, u, noIds.ts));
}

static void follow_judged_on_expected_minutes() {
    // Seen at 3 min, then 4 minutes of failed polls: when a fresh record lacks it, it has arrived, not been lost.
    cta::FollowMemo m;
    view(card(100, 3), m, 100);
    ASSERT_VIEW(FollowState::Arrived, 3, view(card(340, 0, ""), m, 340));
}

static void follow_slid_off_a_full_card() {
    cta::StopRecord full = card(160, 0, "");
    const int16_t mins[4] = {2, 3, 5, 6};
    const char* ids[4] = {"811", "813", "814", "815"};
    for (int i = 0; i < 4; ++i) {
        full.arrivals[i].min = mins[i];
        strcpy(full.arrivals[i].vehicle, ids[i]);
    }
    full.count = 4;
    // Seen at 9; a minute later it would be at 8, past a full card's last arrival (6): still coming.
    cta::FollowMemo m;
    view(card(100, 9), m, 100);
    ASSERT_VIEW(FollowState::Waiting, 9, view(full, m, 160));
    // If the full card's last arrival is later than where it should be, it's gone: Lost.
    full.arrivals[3].min = 12;
    full.ts = 220;
    ASSERT_VIEW(FollowState::Lost, 9, view(full, m, 220));
}

static void follow_empty_card_ends_it() {
    cta::StopRecord empty = card(160, 0, "");
    empty.count = 0;
    cta::FollowMemo m;
    view(card(100, 4), m, 100);
    ASSERT_VIEW(FollowState::Lost, 4, view(empty, m, 160));
}

static void follow_counts_down_across_polls() {
    cta::FollowMemo m;
    for (int k = 0; k <= 8; ++k)
        ASSERT_VIEW(FollowState::Waiting, 9 - k, view(card(100 + 60 * k, 9 - k), m, 100 + 60 * k));
    ASSERT_VIEW(FollowState::Due, 0, view(card(640, 0), m, 640));
    ASSERT_VIEW(FollowState::Arrived, 0, view(card(700, 0, ""), m, 700));
    cta::FollowMemo b;
    view(card(100, 3), b, 100);
    ASSERT_VIEW(FollowState::Lost, 3, view(card(160, 0, ""), b, 160));
}

// Another line's card at the same station (Brown toward Loop), with no 812.
static cta::StopRecord otherLine(uint32_t ts) {
    cta::StopRecord r = card(ts, 0, "");
    strcpy(r.route, "Brn");
    strcpy(r.dir, "Loop");
    return r;
}

static void follow_card_dropped_from_fresh_poll() {
    // (H) Seen at 9; the stop's next poll has other lines but no Red card (no arrivals left): Lost.
    cta::FollowMemo m;
    view(card(100, 9), m, 100);
    cta::StopRecord other = otherLine(50);   // an older poll proves nothing
    ASSERT_VIEW(FollowState::Waiting, 9, cta::followView(&other, 1, follow(), m, 130));
    other = otherLine(100);
    ASSERT_VIEW(FollowState::Waiting, 9, cta::followView(&other, 1, follow(), m, 130));
    other = otherLine(160);
    ASSERT_VIEW(FollowState::Lost, 9, cta::followView(&other, 1, follow(), m, 160));

    // (B) Never seen, and only other lines' cards come in: Lost after the cap of fresh polls.
    cta::FollowMemo u;
    const uint32_t cap = cta::kFollowUnseenCapMin * 60;
    other = otherLine(100);
    ASSERT_VIEW(FollowState::Waiting, -1, cta::followView(&other, 1, follow(), u, 100));
    ASSERT_VIEW(FollowState::Waiting, -1, cta::followView(&other, 1, follow(), u, 100000));   // stale: still waits
    other = otherLine(100 + cap - 1);
    ASSERT_VIEW(FollowState::Waiting, -1, cta::followView(&other, 1, follow(), u, 100100));
    other = otherLine(100 + cap);
    ASSERT_VIEW(FollowState::Lost, -1, cta::followView(&other, 1, follow(), u, 100200));
}

static void follow_unseen_window_starts_at_the_follow() {
    // The card is 600 s old when the follow starts; the first fresh poll a minute later still lacks it.
    cta::FollowMemo m;
    ASSERT_VIEW(FollowState::Waiting, -1, view(card(400, 0, ""), m, 1000));
    ASSERT_VIEW(FollowState::Waiting, -1, view(card(1060, 0, ""), m, 1060));
    ASSERT_VIEW(FollowState::Lost, -1, view(card(1000 + cta::kFollowUnseenCapMin * 60, 0, ""), m, 1180));
}

static void follow_finds_its_card() {
    // Green's longest direction label is 28 bytes; the card keeps 24 of it.
    const char* longDir = "Harlem/Lake (via Marketplace)";
    const cta::StopRecord recs[3] = {card(100, 9, "812", "Howard"), card(100, 5, "812", longDir), card(100, 7)};
    cta::FollowMemo m;
    TEST_ASSERT_EQUAL_INT16(7, cta::followView(recs, 3, follow(), m, 100).min);
    cta::FollowMemo m2;
    TEST_ASSERT_EQUAL_INT16(5, cta::followView(recs, 3, follow(longDir), m2, 100).min);
    cta::StopRecord bus = card(100, 3);
    bus.type = cta::Kind::Bus;  // same id and route, but not this stop
    cta::FollowMemo m3;
    TEST_ASSERT_EQUAL_INT16(-1, cta::followView(&bus, 1, follow(), m3, 100).min);
}

// The wearable's real path: Belmont's recorded response through parseTrainCards (6 cards).
static int belmontCards(cta::StopRecord* cards) {
    const std::string json = readFixture("train_arrivals_belmont_live.json");
    return cta::parseTrainCards(json.c_str(), json.size(), 1000, "41320", cards, 8);
}

static void follow_from_train_cards() {
    cta::StopRecord cards[8];
    const int n = belmontCards(cards);
    TEST_ASSERT_EQUAL(6, n);
    cta::FollowMemo m;
    ASSERT_VIEW(FollowState::Waiting, 1, cta::followView(cards, n, follow("Howard", "815"), m, 1000));
}

static void follow_two_lines_one_label() {
    // Belmont's Loop platform: Brown and Purple both say "Loop". 411 is Brown's; a Purple follow mustn't read it.
    cta::StopRecord cards[8];
    const int n = belmontCards(cards);
    cta::FollowMemo m;
    ASSERT_VIEW(FollowState::Waiting, -1, cta::followView(cards, n, follow("Loop", "411", "P"), m, 1000));
    cta::FollowMemo p;
    ASSERT_VIEW(FollowState::Waiting, 2, cta::followView(cards, n, follow("Loop", "510", "P"), p, 1000));
}

static void follow_expires_after_the_hold() {
    cta::FollowMemo m;
    view(card(100, 0), m, 100);
    TEST_ASSERT_FALSE(cta::followExpired(m, 1000));  // Due isn't over yet
    view(card(200, 0, ""), m, 200);
    TEST_ASSERT_FALSE(cta::followExpired(m, 199 + cta::kFollowEndedSecs));
    TEST_ASSERT_TRUE(cta::followExpired(m, 200 + cta::kFollowEndedSecs));
    cta::FollowMemo lost;
    view(card(100, 9), lost, 100);
    view(card(160, 0, ""), lost, 170);
    TEST_ASSERT_FALSE(cta::followExpired(lost, 169 + cta::kFollowEndedSecs));
    TEST_ASSERT_TRUE(cta::followExpired(lost, 170 + cta::kFollowEndedSecs));
    // A clock still at 0 (before time sync) ends and expires too.
    cta::FollowMemo zero;
    view(card(1, 9), zero, 0);
    ASSERT_VIEW(FollowState::Lost, 9, view(card(61, 0, ""), zero, 0));
    TEST_ASSERT_TRUE(cta::followExpired(zero, cta::kFollowEndedSecs));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(follow_waiting_4_min);
    RUN_TEST(follow_due_at_0);
    RUN_TEST(follow_gone_after_min_1_is_arrived);
    RUN_TEST(follow_gone_at_min_9_is_lost);
    RUN_TEST(follow_failed_poll_keeps_waiting);
    RUN_TEST(follow_card_gone_ends_arrived);
    RUN_TEST(follow_never_seen_ends_lost);
    RUN_TEST(follow_records_without_ids);
    RUN_TEST(follow_judged_on_expected_minutes);
    RUN_TEST(follow_slid_off_a_full_card);
    RUN_TEST(follow_empty_card_ends_it);
    RUN_TEST(follow_counts_down_across_polls);
    RUN_TEST(follow_card_dropped_from_fresh_poll);
    RUN_TEST(follow_unseen_window_starts_at_the_follow);
    RUN_TEST(follow_finds_its_card);
    RUN_TEST(follow_from_train_cards);
    RUN_TEST(follow_two_lines_one_label);
    RUN_TEST(follow_expires_after_the_hold);
    return UNITY_END();
}
