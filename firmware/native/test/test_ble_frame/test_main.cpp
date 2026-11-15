// BLE frames (wearable BLE link plan, "Frames"): a 1-byte header (first, last, 6-bit sequence) on every chunk.
#include <unity.h>

#include <string.h>

#include <string>
#include <vector>

#include "ble_frame.h"

void setUp() {}
void tearDown() {}

using Chunk = std::vector<uint8_t>;

static Chunk chunk(uint8_t header, const std::string& payload) {
    Chunk c{header};
    c.insert(c.end(), payload.begin(), payload.end());
    return c;
}

struct Rx {
    char buf[cta::kMaxBleMsg + 1];
    cta::Reassembler r{buf, sizeof(buf)};
    cta::FrameResult add(const Chunk& c) { return r.add(c.data(), c.size()); }
    std::string msg() const { return std::string(r.message(), r.length()); }
};

static void single_chunk_has_both_bits() {
    Rx rx;
    TEST_ASSERT_EQUAL(cta::FrameResult::Done, rx.add(chunk(cta::kFrameFirst | cta::kFrameLast, "POST /pin\n{}")));
    TEST_ASSERT_EQUAL_STRING("POST /pin\n{}", rx.msg().c_str());
    TEST_ASSERT_EQUAL_STRING("POST /pin\n{}", rx.r.message());   // NUL-terminated
}

static void three_chunks_reassemble() {
    Rx rx;
    TEST_ASSERT_EQUAL(cta::FrameResult::More, rx.add(chunk(cta::kFrameFirst | 0, "abc")));
    TEST_ASSERT_EQUAL(cta::FrameResult::More, rx.add(chunk(1, "def")));
    TEST_ASSERT_EQUAL(cta::FrameResult::Done, rx.add(chunk(cta::kFrameLast | 2, "gh")));
    TEST_ASSERT_EQUAL_STRING("abcdefgh", rx.msg().c_str());
}

static void gap_drops_message() {
    Rx rx;
    rx.add(chunk(cta::kFrameFirst, "abc"));
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(2, "def")));             // 1 is missing
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(cta::kFrameLast | 3, "g")));   // still dropped
    rx.add(chunk(cta::kFrameFirst, "x"));
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(cta::kFrameLast | 0, "y")));   // a duplicate
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(Chunk{}));                           // no header
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(cta::kFrameFirst | 1, "z")));  // a first must be 0
}

static void continuation_first_is_error() {
    Rx rx;
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(0, "abc")));
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(cta::kFrameLast | 1, "d")));
    rx.add(chunk(cta::kFrameFirst | cta::kFrameLast, "done"));
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(1, "after")));   // nothing open after Done
}

static void overflow_at_max_plus_one() {
    Rx rx;
    const std::string part(cta::kMaxBleMsg / 2, 'a');
    rx.add(chunk(cta::kFrameFirst, part));
    TEST_ASSERT_EQUAL(cta::FrameResult::Done, rx.add(chunk(cta::kFrameLast | 1, part)));
    TEST_ASSERT_EQUAL_UINT32(cta::kMaxBleMsg, rx.r.length());
    rx.add(chunk(cta::kFrameFirst, part));
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, rx.add(chunk(cta::kFrameLast | 1, part + "b")));
    // a smaller buffer caps at its own size (less one for the NUL)
    char small[8];
    cta::Reassembler r(small, sizeof(small));
    const Chunk seven = chunk(cta::kFrameFirst | cta::kFrameLast, "1234567");
    const Chunk eight = chunk(cta::kFrameFirst | cta::kFrameLast, "12345678");
    TEST_ASSERT_EQUAL(cta::FrameResult::Done, r.add(seven.data(), seven.size()));
    TEST_ASSERT_EQUAL(cta::FrameResult::Error, r.add(eight.data(), eight.size()));
}

static void sequence_wraps_after_64() {
    Rx rx;
    std::string want;
    for (int i = 0; i < 64; ++i) {
        const std::string p(1, static_cast<char>('A' + i % 26));
        want += p;
        uint8_t h = static_cast<uint8_t>(i & cta::kFrameSeqMask);
        if (i == 0) h |= cta::kFrameFirst;
        TEST_ASSERT_EQUAL(cta::FrameResult::More, rx.add(chunk(h, p)));
    }
    TEST_ASSERT_EQUAL(cta::FrameResult::Done, rx.add(chunk(cta::kFrameLast | 0, "!")));   // 64 wraps to 0
    TEST_ASSERT_EQUAL_STRING((want + "!").c_str(), rx.msg().c_str());
}

static void first_chunk_resets_half_message() {
    Rx rx;
    rx.add(chunk(cta::kFrameFirst, "old"));
    rx.add(chunk(1, "half"));
    TEST_ASSERT_EQUAL(cta::FrameResult::More, rx.add(chunk(cta::kFrameFirst, "new")));
    TEST_ASSERT_EQUAL(cta::FrameResult::Done, rx.add(chunk(cta::kFrameLast | 1, "!")));
    TEST_ASSERT_EQUAL_STRING("new!", rx.msg().c_str());
}

static bool collect(const uint8_t* c, size_t n, void* ctx) {
    static_cast<std::vector<Chunk>*>(ctx)->emplace_back(c, c + n);
    return true;
}

static void split_then_reassemble_round_trips() {
    std::string msg = "POST /stop\n";
    while (msg.size() < cta::kMaxBleMsg) msg += static_cast<char>('a' + msg.size() % 26);
    for (size_t mtu : {20, 185, 509}) {
        const size_t chunkMax = mtu - 3;   // ATT value: MTU - 3
        for (size_t len : {size_t(0), size_t(1), chunkMax - 1, chunkMax, size_t(cta::kMaxBleMsg)}) {
            std::vector<Chunk> chunks;
            TEST_ASSERT_TRUE(cta::splitFrames(msg.data(), len, chunkMax, collect, &chunks));
            TEST_ASSERT_EQUAL_UINT32(len == 0 ? 1 : (len + chunkMax - 2) / (chunkMax - 1), chunks.size());
            Rx rx;
            for (size_t i = 0; i < chunks.size(); ++i) {
                TEST_ASSERT_TRUE(chunks[i].size() <= chunkMax);
                TEST_ASSERT_EQUAL(i + 1 == chunks.size() ? cta::FrameResult::Done : cta::FrameResult::More,
                                  rx.add(chunks[i]));
            }
            TEST_ASSERT_TRUE(rx.msg() == msg.substr(0, len));
        }
    }
    std::vector<Chunk> chunks;
    TEST_ASSERT_TRUE(cta::splitFrames("hi", 2, 3, collect, &chunks));
    TEST_ASSERT_EQUAL_UINT8(cta::kFrameFirst | cta::kFrameLast, chunks[0][0]);
    TEST_ASSERT_FALSE(cta::splitFrames(msg.data(), cta::kMaxBleMsg + 1, 182, collect, &chunks));
    TEST_ASSERT_FALSE(cta::splitFrames("hi", 2, 1, collect, &chunks));
    auto stop = [](const uint8_t*, size_t, void*) { return false; };
    TEST_ASSERT_FALSE(cta::splitFrames("hi", 2, 2, stop, nullptr));   // emit's failure stops it
    // a chunkMax over one ATT value is held to kBleMaxPayload
    chunks.clear();
    TEST_ASSERT_TRUE(cta::splitFrames(msg.data(), 1000, 4000, collect, &chunks));
    TEST_ASSERT_EQUAL_UINT32(512, chunks[0].size());
}

// The same vectors as CTAKit's BLEFrameTests.
static void vectors_match_ctakit() {
    std::vector<Chunk> c;
    TEST_ASSERT_TRUE(cta::splitFrames("", 0, 20, collect, &c));
    TEST_ASSERT_TRUE((c == std::vector<Chunk>{{0xC0}}));
    c.clear();
    TEST_ASSERT_TRUE(cta::splitFrames("hi", 2, 20, collect, &c));
    TEST_ASSERT_TRUE((c == std::vector<Chunk>{{0xC0, 0x68, 0x69}}));
    c.clear();
    TEST_ASSERT_TRUE(cta::splitFrames("abcdefgh", 8, 4, collect, &c));
    TEST_ASSERT_TRUE(c == (std::vector<Chunk>{{0x80, 'a', 'b', 'c'}, {0x01, 'd', 'e', 'f'}, {0x42, 'g', 'h'}}));
    c.clear();
    const std::string b65(65, 'x');
    TEST_ASSERT_TRUE(cta::splitFrames(b65.data(), 65, 2, collect, &c));
    TEST_ASSERT_EQUAL_UINT32(65, c.size());
    TEST_ASSERT_EQUAL_HEX8(0x80, c[0][0]);
    for (int i = 1; i < 64; ++i) TEST_ASSERT_EQUAL_HEX8(i, c[i][0]);   // up to 0x3F
    TEST_ASSERT_EQUAL_HEX8(0x40, c[64][0]);                            // wraps to 0, last
    Rx rx;
    for (size_t i = 0; i < c.size(); ++i) rx.add(c[i]);
    TEST_ASSERT_TRUE(rx.msg() == b65);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(single_chunk_has_both_bits);
    RUN_TEST(three_chunks_reassemble);
    RUN_TEST(gap_drops_message);
    RUN_TEST(continuation_first_is_error);
    RUN_TEST(overflow_at_max_plus_one);
    RUN_TEST(sequence_wraps_after_64);
    RUN_TEST(first_chunk_resets_half_message);
    RUN_TEST(split_then_reassemble_round_trips);
    RUN_TEST(vectors_match_ctakit);
    return UNITY_END();
}
