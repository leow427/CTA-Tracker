#pragma once
// Arduino's millis() and micros(): esp_timer's microsecond clock, with the same 32-bit wraps.
#include <stdint.h>

#include "esp_timer.h"

static inline uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
static inline uint32_t nowUs() { return static_cast<uint32_t>(esp_timer_get_time()); }
