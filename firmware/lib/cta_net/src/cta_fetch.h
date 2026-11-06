#pragma once
// HTTPS GET with TLS verified against the core's built-in CA bundle (never disabled). The URL may hold an
// API key, so it is never logged. Returns the body length (NUL-terminated in buf), or
//   -1 connect / TLS failure or timeout, -2 non-200 status, -3 body doesn't fit in cap - 1.
// Large responses (bus patterns) should go into PSRAM buffers.
#include <stddef.h>
#include <stdint.h>

namespace cta {
int ctaGet(const char* url, char* buf, size_t cap, uint32_t timeoutMs = 8000);
}
