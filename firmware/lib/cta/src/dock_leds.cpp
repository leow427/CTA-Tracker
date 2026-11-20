#include "dock_leds.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cta_config.h"

namespace cta {

namespace {
uint32_t scaled(uint32_t rgb, float k) {
    auto ch = [&](int s) { return static_cast<uint32_t>(lroundf(((rgb >> s) & 255) * k)) << s; };
    return ch(16) | ch(8) | ch(0);
}
}  // namespace

void ledFrame(const LedRow rows[kLedRows], uint32_t now, bool night, uint8_t maxBrightness, uint32_t ms,
              uint32_t out[kLedCount]) {
    const float cap = maxBrightness / 255.f;
    const float phase = (ms % kLedBreathMs) * (6.2831853f / kLedBreathMs);   // reduced first: exact at any uptime
    const float breath = 0.45f + 0.55f * (0.5f + 0.5f * sinf(phase));
    for (int r = 0; r < kLedRows; ++r) {
        const LedRow& row = rows[r];
        const bool fresh = !night && row.ts != 0 && now >= row.ts && now - row.ts <= kLedStaleSec;
        for (int i = 0; i < kLedsPerRow; ++i) {
            float k = 0;
            if (fresh && i == kLedsPerRow - 1 && row.due) k = breath;
            else if (fresh && (row.mask >> i) & 1) k = 1;
            out[ledIndex(r, i)] = k > 0 ? scaled(row.rgb, k * cap) : 0;
        }
    }
}

}  // namespace cta

namespace cta {

int formatLedState(char* out, int n, const LedRow rows[kLedRows], uint32_t now, bool night) {
    int len = snprintf(out, n, "%lu %d", (unsigned long)now, night ? 1 : 0);
    for (int r = 0; r < kLedRows; ++r)
        len += snprintf(len < n ? out + len : nullptr, len < n ? n - len : 0, " %u %lu %lu %d", rows[r].mask,
                        (unsigned long)rows[r].rgb, (unsigned long)rows[r].ts, rows[r].due ? 1 : 0);
    return len;
}

bool parseLedState(const char* s, LedRow rows[kLedRows], uint32_t* now, bool* night) {
    constexpr int kFields = 2 + 4 * kLedRows;
    // Field limits: now, night, then mask, rgb, ts, due per row.
    static const unsigned long kMax[4] = {255, 0xFFFFFF, 0xFFFFFFFF, 1};
    unsigned long v[kFields];
    for (int i = 0; i < kFields; ++i) {
        while (*s == ' ') ++s;
        if (*s < '0' || *s > '9') return false;   // no sign, no empty field
        char* end;
        errno = 0;
        v[i] = strtoul(s, &end, 10);
        const unsigned long max = i == 0 ? 0xFFFFFFFF : i == 1 ? 1 : kMax[(i - 2) % 4];
        if (errno || v[i] > max || (*end != ' ' && *end != '\0' && *end != '\n' && *end != '\r')) return false;
        s = end;
    }
    while (*s == ' ' || *s == '\r' || *s == '\n') ++s;
    if (*s) return false;
    *now = v[0];
    *night = v[1];
    for (int r = 0; r < kLedRows; ++r) {
        const unsigned long* f = v + 2 + 4 * r;
        rows[r].mask = f[0];
        rows[r].rgb = f[1];
        rows[r].ts = f[2];
        rows[r].due = f[3];
    }
    return true;
}

}  // namespace cta
