// Pairing over the network (ecosystem spec 4.3, plan Task 7): the POST /pair body and the gate both boards use.
#include <unity.h>

#include <string.h>

#include <string>

#include "cta_config.h"
#include "pair.h"

void setUp() {}
void tearDown() {}

static const char* const kTok = "0123456789abcdef0123456789abcdef";

static cta::PairGate gate() {
    cta::PairGate g;
    strcpy(g.code, "042917");
    return g;
}

static bool parse(const std::string& json, cta::PairMsg& out) { return cta::parsePair(json.c_str(), json.size(), out); }
static std::string body(const char* code, const char* token) {
    return std::string("{\"code\":\"") + code + "\",\"token\":\"" + token + "\"}";
}

static void right_code_pairs() {
    cta::PairGate g = gate();
    TEST_ASSERT_EQUAL(cta::PairResult::Ok, g.check("042917", 1000, false));
    TEST_ASSERT_EQUAL_UINT8(0, g.wrong);
}

static void wrong_code_counts() {
    cta::PairGate g = gate();
    TEST_ASSERT_EQUAL(cta::PairResult::WrongCode, g.check("042918", 1000, false));
    TEST_ASSERT_EQUAL(cta::PairResult::WrongCode, g.check("142917", 1000, false));
    TEST_ASSERT_EQUAL_UINT8(2, g.wrong);
    TEST_ASSERT_EQUAL(cta::PairResult::Ok, g.check("042917", 1000, false));   // still open after 2
}

static void cools_down_after_5_wrong_then_needs_a_new_code() {
    cta::PairGate g = gate();
    TEST_ASSERT_FALSE(g.needsCode(1000));
    for (int i = 0; i < cta::kPairMaxWrong; ++i) TEST_ASSERT_EQUAL(cta::PairResult::WrongCode, g.check("000000", 1000, false));
    TEST_ASSERT_FALSE(g.open(1000));
    TEST_ASSERT_EQUAL_STRING("", g.code);   // the guessed-at code is gone
    TEST_ASSERT_EQUAL(cta::PairResult::CoolingDown, g.check("042917", 1001, false));   // the old code, too late
    TEST_ASSERT_EQUAL_UINT32(30, g.retryAfterSec(1000));
    TEST_ASSERT_EQUAL_UINT32(1, g.retryAfterSec(1000 + cta::kPairCooldownMs - 1));   // rounded up
    TEST_ASSERT_FALSE(g.needsCode(1000 + cta::kPairCooldownMs - 1));
    // 30 s on: the board picks a new code, and the count starts again
    const uint32_t later = 1000 + cta::kPairCooldownMs;
    TEST_ASSERT_EQUAL_UINT32(0, g.retryAfterSec(later));
    TEST_ASSERT_TRUE(g.needsCode(later));
    TEST_ASSERT_EQUAL(cta::PairResult::WrongCode, g.check("042917", later, false));   // "" never matches
    g.setCode("731005");
    TEST_ASSERT_FALSE(g.needsCode(later));
    TEST_ASSERT_TRUE(g.open(later));
    for (int i = 0; i < cta::kPairMaxWrong - 1; ++i) g.check("000000", later, false);
    TEST_ASSERT_EQUAL(cta::PairResult::Ok, g.check("731005", later, false));
}

static void open_until_paired() {
    cta::PairGate g = gate();
    const uint32_t hours = 6u * 3600u * 1000u;   // no window: an unpaired board waits as long as it takes
    TEST_ASSERT_TRUE(g.open(hours));
    TEST_ASSERT_EQUAL(cta::PairResult::Ok, g.check("042917", hours, false));
    // the cooldown survives the millisecond clock wrapping (49.7 days)
    cta::PairGate w = gate();
    const uint32_t nearWrap = 0xFFFFFFFFu - 1000;
    for (int i = 0; i < cta::kPairMaxWrong; ++i) w.check("000000", nearWrap, false);
    TEST_ASSERT_EQUAL(cta::PairResult::CoolingDown, w.check("042917", 5000, false));   // wrapped, 6 s in
    TEST_ASSERT_TRUE(w.needsCode(nearWrap + cta::kPairCooldownMs));
}

static void reset_forgets_the_code_and_count() {
    cta::PairGate g = gate();
    for (int i = 0; i < cta::kPairMaxWrong; ++i) g.check("000000", 1000, false);
    g.reset();   // "Pair again", or after the board forgets its token
    TEST_ASSERT_EQUAL_UINT8(0, g.wrong);
    TEST_ASSERT_EQUAL_STRING("", g.code);
    TEST_ASSERT_EQUAL_UINT32(0, g.retryAfterSec(1001));   // no cooldown either
    TEST_ASSERT_TRUE(g.needsCode(1001));
    g.setCode("555123");
    TEST_ASSERT_EQUAL(cta::PairResult::Ok, g.check("555123", 1001, false));
    // a few typos before pairing don't carry over to the next code
    for (void (*fresh)(cta::PairGate&) : {+[](cta::PairGate& x) { x.reset(); x.setCode("246810"); },
                                         +[](cta::PairGate& x) { x.setCode("246810"); }}) {
        cta::PairGate t = gate();
        t.check("000000", 1000, false);
        t.check("000000", 1000, false);
        fresh(t);
        TEST_ASSERT_EQUAL_UINT8(0, t.wrong);
        for (int i = 0; i < cta::kPairMaxWrong - 1; ++i) t.check("000000", 1000, false);
        TEST_ASSERT_EQUAL(cta::PairResult::Ok, t.check("246810", 1000, false));
    }
}

static void paired_answers_already() {
    cta::PairGate g = gate();
    TEST_ASSERT_EQUAL(cta::PairResult::AlreadyPaired, g.check("042917", 1000, true));
    TEST_ASSERT_EQUAL(cta::PairResult::AlreadyPaired, g.check("000000", 1000, true));
    TEST_ASSERT_EQUAL_UINT8(0, g.wrong);
    for (int i = 0; i < cta::kPairMaxWrong; ++i) g.check("000000", 1000, false);
    TEST_ASSERT_EQUAL(cta::PairResult::AlreadyPaired, g.check("042917", 1001, true));   // before the cooldown
}

static void no_code_never_pairs() {
    cta::PairGate g;   // a board that never set its code
    TEST_ASSERT_EQUAL(cta::PairResult::WrongCode, g.check("000000", 1000, false));
}

static void parse_takes_code_and_token() {
    cta::PairMsg m;
    TEST_ASSERT_TRUE(parse(body("042917", kTok), m));
    TEST_ASSERT_EQUAL_STRING("042917", m.code);
    TEST_ASSERT_EQUAL_STRING(kTok, m.token);
}

static void parse_rejects_bad_code() {
    cta::PairMsg m;
    for (const char* code : {"04291", "0429170", "04291a", "", " 42917", "04-917"})
        TEST_ASSERT_FALSE_MESSAGE(parse(body(code, kTok), m), code);
    TEST_ASSERT_FALSE(parse(std::string("{\"code\":42917,\"token\":\"") + kTok + "\"}", m));   // a number
    TEST_ASSERT_FALSE(parse(std::string("{\"token\":\"") + kTok + "\"}", m));                  // missing
}

static void parse_rejects_bad_token() {
    cta::PairMsg m;
    for (const char* tok : {"0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdef0",
                            "0123456789ABCDEF0123456789ABCDEF", "0123456789abcdef0123456789abcdeg", ""})
        TEST_ASSERT_FALSE_MESSAGE(parse(body("042917", tok), m), tok);
    TEST_ASSERT_FALSE(parse("{\"code\":\"042917\"}", m));
}

static void bad_json_leaves_state() {
    cta::PairMsg m;
    TEST_ASSERT_TRUE(parse(body("042917", kTok), m));
    for (const char* bad : {"{oops", "[]", "\"042917\"", "null", ""}) TEST_ASSERT_FALSE_MESSAGE(parse(bad, m), bad);
    TEST_ASSERT_FALSE(parse(body("999999", "ffff"), m));
    TEST_ASSERT_EQUAL_STRING("042917", m.code);
    TEST_ASSERT_EQUAL_STRING(kTok, m.token);
}

static void rejects_oversize() {
    std::string b = body("042917", kTok);
    b.insert(1, cta::kMaxBody + 1 - b.size(), ' ');   // the same body padded to one byte over
    TEST_ASSERT_EQUAL_size_t(cta::kMaxBody + 1, b.size());
    cta::PairMsg m;
    TEST_ASSERT_FALSE(parse(b, m));
    b.erase(1, 1);
    TEST_ASSERT_TRUE(parse(b, m));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(right_code_pairs);
    RUN_TEST(wrong_code_counts);
    RUN_TEST(cools_down_after_5_wrong_then_needs_a_new_code);
    RUN_TEST(open_until_paired);
    RUN_TEST(reset_forgets_the_code_and_count);
    RUN_TEST(paired_answers_already);
    RUN_TEST(no_code_never_pairs);
    RUN_TEST(parse_takes_code_and_token);
    RUN_TEST(parse_rejects_bad_code);
    RUN_TEST(parse_rejects_bad_token);
    RUN_TEST(bad_json_leaves_state);
    RUN_TEST(rejects_oversize);
    return UNITY_END();
}
