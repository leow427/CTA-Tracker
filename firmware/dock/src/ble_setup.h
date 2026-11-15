#pragma once
// Bluetooth setup over NimBLE (ESP-IDF). D4 of the ESP-IDF port: just enough to measure its RAM: advertise
// "CTA-Dock-xxxx" with the service of ble_ids.h and serve `info` (the same JSON as GET /info). Pairing and the Wi-Fi
// and token writes come with D5. Started and stopped from the loop (serial "b"); stop() frees the host and the
// controller, so it can start again.
#include <stdint.h>

namespace ble_setup {

bool start(const char* name);
void stop();
bool running();

}  // namespace ble_setup
