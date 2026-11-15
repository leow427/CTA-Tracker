#pragma once
// Pairing over the network (ecosystem spec 4.3, plan Task 7): a board with no token shows a 6-digit code and takes
// POST /pair {"code","token"} until it pairs, with no time limit. After kPairMaxWrong wrong codes the code is dropped
// for kPairCooldownMs, then the board picks a new one (needsCode). The gate is the same on both boards; each board picks
// the codes (esp_random), maps the results to HTTP (Ok 200, WrongCode 403, AlreadyPaired 409, CoolingDown 423 with
// {"error":"cooling down","retry_after":<s>}) and saves the token itself.
#include <stddef.h>
#include <stdint.h>

#include "link_msg.h"

namespace cta {

constexpr int kPairCodeLen = 6;

struct PairMsg {
    char code[kPairCodeLen + 1];
    char token[kTokenLen + 1];
};
// {"code":"<6 digits>","token":"<32 lowercase hex>"}; out is left alone unless it parses.
bool parsePair(const char* json, size_t len, PairMsg& out);

enum class PairResult : uint8_t { Ok, WrongCode, CoolingDown, AlreadyPaired };

struct PairGate {
    char code[kPairCodeLen + 1] = {};   // shown on screen, never logged; "" never matches
    uint8_t wrong = 0;                  // wrong codes since this code was set
    bool cooling = false;
    uint32_t coolStartMs = 0;
    // A new code (6 digits): the count starts again.
    void setCode(const char* c);
    // "Pair again": no code, no count, no cooldown, so needsCode is true. The board clears its token or bond itself.
    void reset();
    // True when the board should pick a code and setCode it: there's none and no cooldown is running.
    bool needsCode(uint32_t nowMs) const;
    // True while a try can pair: a code is set and no cooldown is running.
    bool open(uint32_t nowMs) const;
    // Whole seconds left in the cooldown, rounded up; 0 when none is running.
    uint32_t retryAfterSec(uint32_t nowMs) const;
    // In order: paired -> AlreadyPaired; cooling down -> CoolingDown; wrong code -> WrongCode (counted; the
    // kPairMaxWrong-th drops the code and starts the cooldown); else Ok. code is parsePair's 6 digits; the compare takes
    // the same time however much of it matches. nowMs is milliseconds since boot (wrapping is fine).
    PairResult check(const char* code, uint32_t nowMs, bool paired);
};

}  // namespace cta
