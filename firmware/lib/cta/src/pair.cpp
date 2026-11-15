#include "pair.h"

#include <string.h>

#include "cta_config.h"
#include "json_util.h"
#include "text.h"

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

namespace {
bool coolingAt(const PairGate& g, uint32_t nowMs) { return g.cooling && nowMs - g.coolStartMs < kPairCooldownMs; }
}  // namespace

void PairGate::setCode(const char* c) {
    copyTruncated(code, c, kPairCodeLen);
    wrong = 0;
    cooling = false;
}

void PairGate::reset() {
    code[0] = '\0';
    wrong = 0;
    cooling = false;
}

bool PairGate::needsCode(uint32_t nowMs) const { return code[0] == '\0' && !coolingAt(*this, nowMs); }

bool PairGate::open(uint32_t nowMs) const { return code[0] != '\0' && !coolingAt(*this, nowMs); }

uint32_t PairGate::retryAfterSec(uint32_t nowMs) const {
    return coolingAt(*this, nowMs) ? (kPairCooldownMs - (nowMs - coolStartMs) + 999) / 1000 : 0;
}

PairResult PairGate::check(const char* given, uint32_t nowMs, bool paired) {
    if (paired) return PairResult::AlreadyPaired;
    if (coolingAt(*this, nowMs)) return PairResult::CoolingDown;
    uint8_t diff = code[0] == 0;   // no early exit: the time doesn't depend on how much of the code matched
    for (int i = 0; i < kPairCodeLen; ++i) diff |= static_cast<uint8_t>(code[i] ^ given[i]);
    if (diff == 0) return PairResult::Ok;
    if (code[0] != 0 && ++wrong >= kPairMaxWrong) {   // the code was being guessed at: drop it, wait, then a new one
        code[0] = '\0';
        wrong = 0;
        cooling = true;
        coolStartMs = nowMs;
    }
    return PairResult::WrongCode;
}

}  // namespace cta
