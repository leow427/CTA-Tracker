#include "pair.h"

#include <string.h>

#include "cta_config.h"
#include "json_util.h"

namespace cta {

bool parsePair(const char* json, size_t len, PairMsg& out) {
    if (json == nullptr || len > kMaxBody) return false;
    json::Doc doc(json::parse(json, len));
    const char* code = json::str(json::at(doc.root, "code"));
    const char* token = json::str(json::at(doc.root, "token"));
    if (code == nullptr || strlen(code) != kPairCodeLen || !validToken(token)) return false;
    for (int i = 0; i < kPairCodeLen; ++i)
        if (code[i] < '0' || code[i] > '9') return false;
    memcpy(out.code, code, sizeof(out.code));
    memcpy(out.token, token, sizeof(out.token));
    return true;
}

bool PairGate::open(uint32_t nowMs) const { return wrong < kPairMaxWrong && nowMs <= kPairWindowMs; }

PairResult PairGate::check(const char* given, uint32_t nowMs, bool paired) {
    if (paired) return PairResult::AlreadyPaired;
    if (wrong >= kPairMaxWrong) return PairResult::Locked;
    if (nowMs > kPairWindowMs) return PairResult::Closed;
    uint8_t diff = code[0] == 0;   // no early exit: the time doesn't depend on how much of the code matched
    for (int i = 0; i < kPairCodeLen; ++i) diff |= static_cast<uint8_t>(code[i] ^ given[i]);
    if (diff) {
        ++wrong;
        return PairResult::WrongCode;
    }
    return PairResult::Ok;
}

}  // namespace cta
