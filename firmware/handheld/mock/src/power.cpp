#ifdef HANDHELD_LIVE   // the mock build has no power chip code
// The AXP2101 registers are XPowersLib 0.3.3's (src/XPowersAXP2101.tpp and REG/AXP2101Constants.h, MIT License,
// Copyright (c) 2022 lewis he): permission is granted to use, copy, modify and distribute it, provided this notice is
// kept; the software is provided "as is", without warranty of any kind.
#include "power.h"

#include "driver/i2c_master.h"

namespace power {

namespace {

constexpr uint8_t kAddr = 0x34, kChipId = 0x4A;
constexpr uint8_t kStatus1 = 0x00, kStatus2 = 0x01, kIcType = 0x03, kCommonConfig = 0x10, kAdcCtrl = 0x30,
                  kBattMvH = 0x34, kVbusMvH = 0x38, kSysMvH = 0x3A, kTempH = 0x3C, kTsPinCtrl = 0x50, kBatDetCtrl = 0x68,
                  kBattPercent = 0xA4;
constexpr int kTimeoutMs = 50;   // Arduino Wire's
i2c_master_dev_handle_t dev = nullptr;
bool ok = false;

int reg(uint8_t r) {   // -1 on a bus error, as XPowersLib's readRegister
    uint8_t v;
    return i2c_master_transmit_receive(dev, &r, 1, &v, 1, kTimeoutMs) == ESP_OK ? v : -1;
}

bool put(uint8_t r, uint8_t v) {
    const uint8_t b[] = {r, v};
    return i2c_master_transmit(dev, b, 2, kTimeoutMs) == ESP_OK;
}

bool setBits(uint8_t r, uint8_t mask, uint8_t bits) {
    const int v = reg(r);
    return v >= 0 && put(r, static_cast<uint8_t>((v & ~mask) | bits));
}

// A 14-bit (high 6) or 13-bit (high 5) ADC result in two registers; 0xFFFF on a bus error.
uint16_t adc(uint8_t high, uint8_t highMask) {
    const int h = reg(high), l = reg(high + 1);
    return h < 0 || l < 0 ? 0xFFFF : static_cast<uint16_t>((h & highMask) << 8 | l);
}

}  // namespace

bool begin(i2c_master_bus_handle_t bus) {
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kAddr;
    cfg.scl_speed_hz = 100000;   // Wire's default, which the Arduino build ran at
    if (!dev && i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return false;
    ok = reg(kIcType) == kChipId;
    if (!ok) return false;
    // XPowersLib's begin: the TS pin as a plain input, so it doesn't gate the charger, and its ADC off.
    const int ts = reg(kTsPinCtrl);
    if (ts >= 0) put(kTsPinCtrl, static_cast<uint8_t>((ts & 0xF0) | 0x10));
    setBits(kAdcCtrl, 0x02, 0);
    // Measurements only (battery presence, voltages, fuel gauge, temperature); no rail is touched.
    setBits(kBatDetCtrl, 0x01, 0x01);
    setBits(kAdcCtrl, 0x1D, 0x1D);   // battery (bit 0), VBUS (2), system (3), and chip temperature (4)
    return true;
}

State read() {
    State s;
    if (!ok) return s;
    s.ok = true;
    const int st1 = reg(kStatus1), st2 = reg(kStatus2);
    s.usb = st1 >= 0 && st2 >= 0 && !(st2 & 0x08) && (st1 & 0x20);   // VBUS present and good
    s.battery = st1 >= 0 && (st1 & 0x08);
    s.charging = s.battery && st2 >= 0 && (st2 >> 5) == 0x01;
    s.vbusMv = s.usb ? adc(kVbusMvH, 0x3F) : 0;
    s.sysMv = adc(kSysMvH, 0x3F);
    const uint16_t t = adc(kTempH, 0x3F);
    s.tempC = t > 16383 ? NAN : 22.0f + (7274 - t) / 20.0f;
    if (s.battery) {
        s.percent = reg(kBattPercent);
        const uint16_t mv = adc(kBattMvH, 0x1F);
        s.battMv = mv == 0xFFFF ? 0 : mv;
    }
    return s;
}

bool off() {
    if (!ok) return false;
    setBits(kCommonConfig, 0x01, 0x01);
    return true;
}

}  // namespace power
#endif
