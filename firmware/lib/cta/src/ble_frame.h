#pragma once
// BLE frames (wearable BLE link plan, "Frames"): a message bigger than one ATT write or notify goes as chunks, each
// with a 1-byte header: bit 7 first chunk, bit 6 last chunk, bits 0-5 a sequence that starts at 0 on the first chunk
// and goes up by 1 (mod 64). A single-chunk message sets both bits. Mirrored in the app's BLEFrames.swift.
#include <stddef.h>
#include <stdint.h>

namespace cta {

constexpr size_t kMaxBleMsg = 8192;   // one message, headers not counted
constexpr uint8_t kFrameFirst = 0x80, kFrameLast = 0x40, kFrameSeqMask = 0x3F;

enum class FrameResult : uint8_t { More, Done, Error };

// Joins chunks into buf (the caller's: PSRAM on the board). No allocation. A first chunk resets it; a sequence gap,
// a continuation with nothing open, an empty chunk, or more than kMaxBleMsg (or cap - 1) bytes drops the message
// (Error), and chunks are refused until the next first chunk.
class Reassembler {
public:
    Reassembler(char* buf, size_t cap) : buf_(buf), cap_(cap) {}
    // Done: message() holds the whole message, NUL-terminated, until the next add.
    FrameResult add(const uint8_t* chunk, size_t len);
    const char* message() const { return buf_; }
    size_t length() const { return len_; }

private:
    char* buf_;
    size_t cap_;
    size_t len_ = 0;
    uint8_t next_ = 0;
    bool open_ = false;
};

// Splits msg into chunks of at most chunkMax bytes (header included; held to kBleMaxPayload, one ATT value) and calls
// emit(chunk, len, ctx) for each, in order. An empty message is one header-only chunk. False if msg is over
// kMaxBleMsg, chunkMax < 2, or emit returns false.
bool splitFrames(const char* msg, size_t len, size_t chunkMax, bool (*emit)(const uint8_t*, size_t, void*),
                 void* ctx);

}  // namespace cta
