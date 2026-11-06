#pragma once
// Laptop stand-ins for the few Arduino calls the UI uses (preview build only).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
uint32_t millis();
inline void delay(uint32_t) {}
struct PreviewSerial {
    void println(const char* s) { puts(s); }
    template <typename... A> void printf(const char* f, A... a) { ::printf(f, a...); }
};
extern PreviewSerial Serial;
