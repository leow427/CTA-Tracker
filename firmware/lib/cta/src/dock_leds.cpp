#include "dock_leds.h"

#include <math.h>

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
