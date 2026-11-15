// The register sequences are SensorLib 0.3.1's (src/touch/TouchDrvCST92xx.cpp, MIT License, Copyright (c) 2024 lewis
// he): permission is granted to use, copy, modify and distribute it, provided this notice is kept; the software is
// provided "as is", without warranty of any kind.
#include "cst9217.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cst9217 {

namespace {

constexpr uint8_t kAddr = 0x5A;
constexpr int kTimeoutMs = 50;   // Arduino Wire's
constexpr uint8_t kAck = 0xAB;
constexpr uint8_t kMaxPoints = 2;
i2c_master_dev_handle_t dev = nullptr;

// A write, a stop, then a read: SensorLib's writeThenRead over Wire (endTransmission with a stop, then requestFrom).
bool writeThenRead(const uint8_t* w, size_t wn, uint8_t* r, size_t rn) {
    return i2c_master_transmit(dev, w, wn, kTimeoutMs) == ESP_OK && i2c_master_receive(dev, r, rn, kTimeoutMs) == ESP_OK;
}

uint32_t le32(const uint8_t* b) { return b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24; }

}  // namespace

bool begin(i2c_master_bus_handle_t bus, int rstPin, int intPin) {
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kAddr;
    cfg.scl_speed_hz = 100000;   // Wire's default, which the Arduino build ran at
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return false;
    if (intPin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(intPin));
        gpio_set_direction(static_cast<gpio_num_t>(intPin), GPIO_MODE_INPUT);
    }
    if (rstPin >= 0) {   // reset, then wait for the chip to leave its boot mode
        gpio_reset_pin(static_cast<gpio_num_t>(rstPin));
        gpio_set_direction(static_cast<gpio_num_t>(rstPin), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(rstPin), 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(static_cast<gpio_num_t>(rstPin), 1);
    }
    vTaskDelay(pdMS_TO_TICKS(30));
    const uint8_t cmdMode[] = {0xD1, 0x01};
    if (i2c_master_transmit(dev, cmdMode, sizeof(cmdMode), kTimeoutMs) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t b[8];
    const uint8_t checkReg[] = {0xD1, 0xFC}, typeReg[] = {0xD2, 0x04}, fwReg[] = {0xD2, 0x08};
    if (!writeThenRead(checkReg, 2, b, 4)) return false;
    const uint32_t checkcode = le32(b);
    if (!writeThenRead(typeReg, 2, b, 4)) return false;
    const uint16_t chip = static_cast<uint16_t>(b[2] | b[3] << 8);
    if (!writeThenRead(fwReg, 2, b, 8)) return false;
    const uint32_t fw = le32(b);
    return fw != 0xA5A5A5A5 && (checkcode & 0xffff0000) == 0xCACA0000 && (chip == 0x9217 || chip == 0x9220);
}

uint8_t read(int16_t* x, int16_t* y, uint8_t max) {
    if (!dev) return 0;
    uint8_t buf[kMaxPoints * 5 + 5] = {};
    const uint8_t cmd[] = {0xD0, 0x00, kAck};
    if (!writeThenRead(cmd, 2, buf, sizeof(buf))) return 0;
    if (i2c_master_transmit(dev, cmd, 3, kTimeoutMs) != ESP_OK) return 0;   // the read's ack
    if (buf[6] != kAck) return 0;
    const uint8_t n = buf[5] & 0x7F;
    if (n > kMaxPoints || n == 0) return 0;
    bool firstDown = false;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t* d = buf + i * 5 + (i == 0 ? 0 : 2);
        const bool down = (d[0] & 0x0F) == 0x06 && (d[0] >> 4) < kMaxPoints;
        if (i == 0) firstDown = down;
        if (i < max) {   // a point that isn't down reads as (0, 0), as SensorLib's did
            x[i] = down ? static_cast<int16_t>(d[1] << 4 | d[3] >> 4) : 0;
            y[i] = down ? static_cast<int16_t>(d[2] << 4 | (d[3] & 0x0F)) : 0;
        }
    }
    return firstDown ? n : 0;
}

}  // namespace cst9217
