#pragma once
// Pairing over the network (ecosystem spec 4.3, plan Task 7): a board with no token shows a 6-digit code and takes
// POST /pair {"code","token"} once, within kPairWindowMs of boot, until kPairMaxWrong wrong codes lock it until reboot.
// The gate is the same on both boards; each board maps its results to HTTP (Ok 200, WrongCode 403, AlreadyPaired 409,
// Locked and Closed 423) and saves the token itself.
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

enum class PairResult : uint8_t { Ok, WrongCode, Locked, Closed, AlreadyPaired };

struct PairGate {
    char code[kPairCodeLen + 1] = {};   // set once at boot, shown on screen, never logged; "" never matches
    uint8_t wrong = 0;                  // RAM only, so a reboot clears the lock
    // True while a try can still pair: not locked, and nowMs (since boot) inside the window.
    bool open(uint32_t nowMs) const;
    // In order: paired → AlreadyPaired; locked → Locked; window over → Closed; wrong code → WrongCode (counted);
    // else Ok. code is parsePair's 6 digits; the compare takes the same time however much of it matches.
    PairResult check(const char* code, uint32_t nowMs, bool paired);
};

}  // namespace cta
