#pragma once
// Milliseconds since boot, wrapping at 32 bits as Arduino's millis() did (every timer here compares by subtraction).
#include <esp_timer.h>
#include <stdint.h>

static inline uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
