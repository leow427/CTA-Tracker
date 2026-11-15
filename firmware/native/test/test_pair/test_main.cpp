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

static void locks_after_5_wrong() {
    cta::PairGate g = gate();
    for (int i = 0; i < cta::kPairMaxWrong; ++i) TEST_ASSERT_EQUAL(cta::PairResult::WrongCode, g.check("000000", 1000, false));
    TEST_ASSERT_FALSE(g.open(1000));
    TEST_ASSERT_EQUAL(cta::PairResult::Locked, g.check("042917", 1000, false));   // the 6th try, right code
    TEST_ASSERT_EQUAL_UINT8(cta::kPairMaxWrong, g.wrong);                        // locked tries don't count on
}

static void closed_after_5_minutes() {
    cta::PairGate g = gate();
    TEST_ASSERT_TRUE(g.open(cta::kPairWindowMs));
    TEST_ASSERT_FALSE(g.open(cta::kPairWindowMs + 1));
    TEST_ASSERT_EQUAL(cta::PairResult::Closed, g.check("042917", cta::kPairWindowMs + 1, false));
    TEST_ASSERT_EQUAL(cta::PairResult::Closed, g.check("000000", cta::kPairWindowMs + 1, false));
    TEST_ASSERT_EQUAL_UINT8(0, g.wrong);   // a closed gate doesn't count wrong codes
    cta::PairGate last = gate();
    TEST_ASSERT_EQUAL(cta::PairResult::Ok, last.check("042917", cta::kPairWindowMs, false));   // the last ms is in
}

static void paired_answers_already() {
    cta::PairGate g = gate();
    TEST_ASSERT_EQUAL(cta::PairResult::AlreadyPaired, g.check("042917", 1000, true));
    TEST_ASSERT_EQUAL(cta::PairResult::AlreadyPaired, g.check("000000", 1000, true));
    TEST_ASSERT_EQUAL_UINT8(0, g.wrong);
    for (int i = 0; i < cta::kPairMaxWrong; ++i) g.check("000000", 1000, false);
    TEST_ASSERT_EQUAL(cta::PairResult::AlreadyPaired, g.check("042917", cta::kPairWindowMs + 1, true));   // first
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
    RUN_TEST(locks_after_5_wrong);
    RUN_TEST(closed_after_5_minutes);
    RUN_TEST(paired_answers_already);
    RUN_TEST(no_code_never_pairs);
    RUN_TEST(parse_takes_code_and_token);
    RUN_TEST(parse_rejects_bad_code);
    RUN_TEST(parse_rejects_bad_token);
    RUN_TEST(bad_json_leaves_state);
    RUN_TEST(rejects_oversize);
    return UNITY_END();
}
