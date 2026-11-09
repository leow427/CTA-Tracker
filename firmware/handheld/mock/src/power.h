#pragma once
// The AXP2101 power chip (spec 3, 8.4, 8.5): USB power, battery, and charging. It turns on measurements only and
// never changes a power rail.
#include <stdint.h>

namespace power {

struct State {
    bool ok = false;        // the chip answered
    bool usb = false;       // VBUS present
    bool battery = false;   // a battery is connected
    int percent = -1;       // fuel gauge, -1 with no battery
    bool charging = false;
    uint16_t battMv = 0;
};

// Call after Wire.begin(). False if the chip doesn't answer.
bool begin();
State read();

}  // namespace power
