#pragma once
// The AXP2101 power chip (spec 3, 8.4, 8.5): USB power, battery, and charging. It turns on measurements only and
// changes no power rail, except off().
#include <stdint.h>

namespace power {

struct State {
    bool ok = false;        // the chip answered
    bool usb = false;       // VBUS present
    bool battery = false;   // a battery is connected
    int percent = -1;       // fuel gauge, -1 with no battery
    bool charging = false;
    uint16_t battMv = 0;
    uint16_t vbusMv = 0;    // USB voltage
    uint16_t sysMv = 0;     // system rail
    float tempC = 0;        // the chip's own temperature
};

// Call after Wire.begin(). False if the chip doesn't answer.
bool begin();
State read();
// Turns the whole board off (every rail, screen included), so a board left plugged in can't burn in. Only the task
// that owns the I2C bus may call it (the touch task, or the loop when there's none). False if there's no chip to ask.
bool off();

}  // namespace power
