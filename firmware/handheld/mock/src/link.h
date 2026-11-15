#pragma once
// The wearable's link (ecosystem spec 4.2–4.4; docs/superpowers/plans/2026-11-14-wearable-ble-link.md): one
// dispatcher (apply) for HTTP on the home network and Bluetooth (ble_setup.*). HTTP: GET /info, GET/POST /config (pet,
// settings), POST /nearby, POST/DELETE /pin, POST/DELETE /follow, POST /reset on port 80, announced over mDNS as
// _ctatracker._tcp; its handlers run on the httpd task (core 0) and check the body's size (413) and the token (401)
// first. Bluetooth adds POST /token and POST /wifi (404 over HTTP); its link is already encrypted and bonded. HTTP
// adds POST /pair (no token; 404 over Bluetooth): the Wi-Fi fallback's pairing with the code on the screen, through
// the dock's cta::PairGate (200, 400, 403, 409, 413, 423). A pin,
// follow or Wi-Fi network goes straight to the network task (live::*, RAM only); writes that touch flash (config,
// nearby, token) or reboot wait in an inbox for the frame loop (take). POST /relay and POST /stop (both transports;
// /stop up to kMaxBleMsg over Bluetooth, kMaxBody over HTTP) feed the phone's relay to live::relayTick/relayStop.
// Live build only.
#include <stddef.h>
#include <stdint.h>

#include "link_msg.h"
#include "pair.h"

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
    bool token = false;
    char tokenHex[cta::kTokenLen + 1];   // wiped once the loop has saved it
};

enum class Via : uint8_t { Http, Ble };
// The transport-neutral write: parses body (len bytes, need not end in NUL) and hands it off. Returns the HTTP
// status: 200, 400 (bad body), 404 (no such path, or Bluetooth-only over HTTP), 413 (over kMaxBody). Runs on httpd's
// task or NimBLE's host task, so it only parses and copies into RAM.
int apply(bool del, const char* path, const char* body, size_t len, Via via);

// Once, before anything else here: the mutex and the token writes are checked against ("" = none: every write 401).
void begin(const char* token);
// The httpd server and mDNS (hostLabel "cta-pet-xxxx"). Call once Wi-Fi has an IP; later calls do nothing.
void start(const char* hostLabel);
void setToken(const char* token);
// The loop publishes; GET /info serves the latest copy (uptime and Wi-Fi filled in when it's served).
void setInfo(const cta::InfoMsg& info);
// GET /info's JSON (what the `info` characteristic carries too) into buf; its length, or 0 if it doesn't fit.
size_t infoJson(char* buf, size_t cap);
// What GET /config returns.
void setConfig(const cta::ConfigMsg& c);
// The loop drains writes into out (keep it off internal RAM: it holds a 1 KB body); false if none.
bool take(Inbox& out);
// The Pair screen's Wi-Fi code: Code (copied into code; picked here once httpd is up, again after a cooldown or
// pairAgain), Wait (a cooldown after 5 wrong codes; waitS seconds left) or None (paired, or no Wi-Fi yet). Never logged.
enum class PairShow : uint8_t { None, Code, Wait };
PairShow pairShow(char code[cta::kPairCodeLen + 1], uint8_t& waitS);
// Settings' "Pair again": the token is forgotten (the caller wipes it from NVS) and the gate starts over.
void pairAgain();
// The httpd task's least free stack seen after a request, in bytes (0 = no request yet).
uint32_t stackLeft();

}  // namespace lan
