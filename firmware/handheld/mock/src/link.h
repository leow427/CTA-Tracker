#pragma once
// The wearable's HTTP link on the home network (ecosystem spec 4.2–4.4), the dock's (firmware/dock/src/link.*) plus
// the wearable's endpoints: GET /info, GET/POST /config (pet, settings), POST /nearby, POST/DELETE /pin,
// POST/DELETE /follow, POST /ble, POST /reset on port 80, announced over mDNS as _ctatracker._tcp. Handlers run on
// the httpd task (core 0): they check the body's size (413), the token (401) and the JSON (400), then answer at once.
// A pin or follow goes straight to the network task (live::setPin/setFollow, RAM only); writes that touch flash
// (config, nearby) or reboot wait in an inbox for the frame loop (take). Live build only.
#include <stddef.h>
#include <stdint.h>

#include "link_msg.h"

namespace lan {   // not `link`: newlib declares POSIX link(), and a namespace can't share its name

// Writes waiting for the loop. Configs that arrive before it drains are merged part by part (the newest of each);
// a newer nearby list replaces an older one.
struct Inbox {
    bool config = false;
    cta::ConfigMsg cfg;
    bool nearby = false;
    char nearbyJson[cta::kMaxBody + 1];
    size_t nearbyLen = 0;
    bool reset = false;
    bool openBle = false;
};

// Once, before anything else here: the mutex and the token writes are checked against ("" = none: every write 401).
void begin(const char* token);
// The httpd server and mDNS (hostLabel "cta-pet-xxxx"). Call once Wi-Fi has an IP; later calls do nothing.
void start(const char* hostLabel);
void setToken(const char* token);
// The loop publishes; GET /info serves the latest copy (uptime and Wi-Fi filled in when it's served).
void setInfo(const cta::InfoMsg& info);
// What GET /config returns.
void setConfig(const cta::ConfigMsg& c);
// The loop drains writes into out (keep it off internal RAM: it holds a 1 KB body); false if none.
bool take(Inbox& out);
// The httpd task's least free stack seen after a request, in bytes (0 = no request yet).
uint32_t stackLeft();

}  // namespace lan
