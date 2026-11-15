#pragma once
// The wearable's Bluetooth link on IDF's NimBLE (docs/superpowers/plans/2026-11-14-wearable-ble-link.md, "The link"):
// it advertises the app's service as CTA-Pet-xxxx, with two characteristics. `link` takes the phone's framed
// "POST /pin\n{json}" messages (encrypted, authenticated writes) into lan::apply; `info` carries the status JSON
// (encrypted read, and framed notifies). Passkey pairing (the code on the screen, never logged) and bonds in NVS.
// Always on in the live build; serial "b" turns it off and on. Live build only.
#include <stddef.h>
#include <stdint.h>

namespace ble_setup {

// Starts the controller and host and advertises as name. False if it couldn't start.
bool start(const char* name);
// Stops advertising, drops any connection, and frees the controller and host (it can start again).
void stop();
bool running();
// A phone is connected over an encrypted, authenticated, bonded link.
bool connected();
// A bond is stored.
bool bonded();
// The status JSON for `info` reads. notify: also send it to a subscribed, bonded phone (as it is on its first
// call after a subscribe). The loop calls this once a second; it sends at most one message per call.
void setInfo(const char* json, size_t len, bool notify);
// The pairing code to show (1–999999), or 0 for none.
uint32_t passkey();
// Forgets every bond and the static random address (NVS). Call with Bluetooth stopped; the next start() comes back
// as a new device (a new address), so an iPhone that still holds the old bond pairs afresh.
void forgetBonds();
// The host task's least free stack seen in the link's callbacks, in bytes (0 = none yet).
uint32_t stackLeft();

}  // namespace ble_setup
