#pragma once
// Bluetooth LE on IDF's NimBLE, for now just enough to measure what it costs (ESP-IDF port plan, Task W5): advertise
// the app's service with the wearable's name and serve the `info` characteristic. Setup over BLE (Task W6) builds on
// this. Serial "b" turns it on and off.
#include <stdint.h>

namespace ble_setup {

// Starts the controller and host and advertises as name. False if it couldn't start.
bool start(const char* name);
// Stops advertising, drops any connection, and frees the controller and host (it can start again).
void stop();
bool running();

}  // namespace ble_setup
