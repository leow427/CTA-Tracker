#include "power.h"

#include <Wire.h>

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

namespace power {

namespace {

constexpr int kSda = 15, kScl = 14;   // shared with touch
XPowersPMU pmu;
bool ok = false;

}  // namespace

bool begin() {
    ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, kSda, kScl);
    if (!ok) return false;
    // Measurements only (battery presence, voltage, fuel gauge); no rail is touched.
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    return true;
}

State read() {
    State s;
    if (!ok) return s;
    s.ok = true;
    s.usb = pmu.isVbusIn();
    s.battery = pmu.isBatteryConnect();
    s.charging = s.battery && pmu.isCharging();
    if (s.battery) {
        s.percent = pmu.getBatteryPercent();
        s.battMv = pmu.getBattVoltage();
    }
    return s;
}

}  // namespace power
