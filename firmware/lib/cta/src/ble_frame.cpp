#include "ble_frame.h"

#include <string.h>

#include "cta_config.h"

namespace cta {

FrameResult Reassembler::add(const uint8_t* chunk, size_t len) {
    if (chunk == nullptr || len == 0 || buf_ == nullptr || cap_ == 0) {
        open_ = false;
        return FrameResult::Error;
    }
    const uint8_t h = chunk[0], seq = h & kFrameSeqMask;
    if (h & kFrameFirst) {
        open_ = true;
        len_ = 0;
        next_ = 0;
    }
    const size_t n = len - 1, room = (cap_ - 1 < kMaxBleMsg ? cap_ - 1 : kMaxBleMsg) - len_;
    if (!open_ || seq != next_ || n > room) {
        open_ = false;
        return FrameResult::Error;
    }
    memcpy(buf_ + len_, chunk + 1, n);
    len_ += n;
    buf_[len_] = '\0';
    next_ = (next_ + 1) & kFrameSeqMask;
    if (!(h & kFrameLast)) return FrameResult::More;
    open_ = false;
    return FrameResult::Done;
}

bool splitFrames(const char* msg, size_t len, size_t chunkMax, bool (*emit)(const uint8_t*, size_t, void*),
                 void* ctx) {
    if (len > kMaxBleMsg || chunkMax < 2 || emit == nullptr || (msg == nullptr && len > 0)) return false;
    if (chunkMax > kBleMaxPayload) chunkMax = kBleMaxPayload;
    uint8_t chunk[kBleMaxPayload];
    const size_t step = chunkMax - 1;
    size_t at = 0;
    uint8_t seq = 0;
    do {
        const size_t n = len - at < step ? len - at : step;
        chunk[0] = static_cast<uint8_t>((at == 0 ? kFrameFirst : 0) | (at + n == len ? kFrameLast : 0) | seq);
        if (n > 0) memcpy(chunk + 1, msg + at, n);
        if (!emit(chunk, n + 1, ctx)) return false;
        at += n;
        seq = (seq + 1) & kFrameSeqMask;
    } while (at < len);
    return true;
}

}  // namespace cta
