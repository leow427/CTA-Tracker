#pragma once
// The dock's HTTP link on the home network (ecosystem spec 4.2–4.4): GET /info, GET/POST /config, POST /ble,
// POST /reset on port 80, announced over mDNS as _ctatracker._tcp. Handlers run on the httpd task (core 0): they
// check the body's size (413), the token (401) and the JSON (400), copy a good write into an inbox under a mutex and
// answer at once; the frame loop only drains the inbox (take) and publishes what GET serves (setInfo, setConfig).
#include <stdint.h>

#include "link_msg.h"

namespace lan {

// Writes waiting for the loop. Configs that arrive before it drains are merged part by part (the newest of each).
struct Inbox {
    bool config = false;
    cta::ConfigMsg cfg;
    bool reset = false;
    bool openBle = false;
};

// Once, before anything else here: the mutex and the token writes are checked against ("" = none: every write 401).
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
// The httpd task's least free stack seen after a request, in bytes (0 = no request yet).
uint32_t stackLeft();

}  // namespace lan
