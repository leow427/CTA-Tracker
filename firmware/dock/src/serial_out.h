#pragma once
// Serial output on UART0 (the CH343 port), one driver write per call. The C library's stdout would hand the UART
// driver one character at a time (each a ring-buffer entry of its own, about 32 bytes): slow on the calling core, and
// the perf build's 4 KB TX buffer would hold only about 128 characters. Lines from different tasks never mix.
#include <driver/uart.h>
#include <stdarg.h>
#include <stdio.h>

__attribute__((format(printf, 1, 2))) static inline void uprintf(const char* fmt, ...) {
    char buf[320];   // the longest line (stats) is about 200 characters; longer ones are cut
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= static_cast<int>(sizeof(buf))) n = sizeof(buf) - 1;
    uart_write_bytes(UART_NUM_0, buf, n);
}
