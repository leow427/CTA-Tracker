#pragma once
// The CST9217 touch controller over i2c_master: the reads SensorLib 0.3.1's TouchDrvCST92xx made (begin's attribute
// check, getPoint), and nothing more. Only one task may call these (the bus lock covers a transfer, not a read's
// write-then-read pair).
#include <stdint.h>

#include "driver/i2c_master.h"

namespace cst9217 {

// Resets the chip (rstPin), then checks its firmware and chip type, as SensorLib's begin did. False if it doesn't
// answer as a CST9217/9220.
bool begin(i2c_master_bus_handle_t bus, int rstPin, int intPin);
// Up to max (at most 2) touch points in panel coordinates; 0 with none, on a read error, or when the chip reports
// more points than it supports (SensorLib's getPoint, unchanged).
uint8_t read(int16_t* x, int16_t* y, uint8_t max);

}  // namespace cst9217
