#pragma once
// The dock's HTTP link on the home network (ecosystem spec 4.2–4.4): GET /info, GET/POST /config, POST /pair,
// POST /reset on port 80, announced over mDNS as _ctatracker._tcp. Handlers run on the httpd task (core 0): they
// check the body's size (413), the token (401) and the JSON (400), copy a good write into an inbox under a mutex and
// answer at once; the frame loop only drains the inbox (take) and publishes what GET serves (setInfo, setConfig).
#include <stddef.h>
#include <stdint.h>

#include "link_msg.h"
#include "pair.h"

namespace lan {

// Writes waiting for the loop. Configs that arrive before it drains are merged part by part (the newest of each).
struct Inbox {
    bool config = false;
    cta::ConfigMsg cfg;
    bool reset = false;
    bool paired = false;   // POST /pair took this token (already in use; the loop saves it)
    char token[cta::kTokenLen + 1] = "";
};

// What the setup screen shows about pairing: nothing (paired), the code, or the cooldown after 5 wrong codes (then a
// new code). An unpaired dock stays in pairing until it's paired: no time window.
enum class PairShow : uint8_t { None, Code, Wait };

// Once, before anything else here: the mutex and the token writes are checked against ("" = none: every write 401,
// and POST /pair opens with a new code once Wi-Fi is up).
void begin(const char* token);
// The httpd server and mDNS (hostLabel "cta-dock-xxxx"). Call once Wi-Fi is up; later calls do nothing.
void start(const char* hostLabel);
void setToken(const char* token);
// The loop publishes; GET /info serves the latest copy (uptime and Wi-Fi filled in when it's served).
void setInfo(const cta::InfoMsg& info);
// What GET /config returns.
void setConfig(const cta::ConfigMsg& c);
// The loop drains writes; false if none.
bool take(Inbox& out);
// The pairing state now, the code to show (filled for Code only), and the cooldown's seconds left (for Wait).
PairShow pairShow(char code[cta::kPairCodeLen + 1], uint8_t& waitS);
// Has a token.
bool paired();
// Settings' "Pair again": forgets the token in RAM and starts pairing over with a fresh code (the caller wipes NVS).
void pairAgain();
// The httpd task's least free stack seen after a request, in bytes (0 = no request yet).
uint32_t stackLeft();

}  // namespace lan
