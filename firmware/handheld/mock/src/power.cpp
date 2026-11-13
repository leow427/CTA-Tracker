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
    // Measurements only (battery presence, voltages, fuel gauge, temperature); no rail is touched.
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu.enableTemperatureMeasure();
    return true;
}

State read() {
    State s;
    if (!ok) return s;
    s.ok = true;
    s.usb = pmu.isVbusIn();
    s.battery = pmu.isBatteryConnect();
    s.charging = s.battery && pmu.isCharging();
    s.vbusMv = s.usb ? pmu.getVbusVoltage() : 0;
    s.sysMv = pmu.getSystemVoltage();
    s.tempC = pmu.getTemperature();
    if (s.battery) {
        s.percent = pmu.getBatteryPercent();
        s.battMv = pmu.getBattVoltage();
    }
    return s;
}

bool off() {
    if (!ok) return false;
    pmu.shutdown();
    return true;
}

}  // namespace power
