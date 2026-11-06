#include "leds.h"

#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "cta_config.h"
#include "line_colors.h"
#include "pins.h"
#include "state.h"

namespace leds {

static Adafruit_NeoPixel sStrip(LED_ROWS * LED_PER_ROW, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t sLast[LED_ROWS * LED_PER_ROW];

void begin() {
    sStrip.begin();
    sStrip.clear();
    sStrip.show();
}

static uint32_t scaled(uint32_t rgb, uint32_t level) {   // level 0-255
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) out |= ((((rgb >> shift) & 0xFF) * level / 255) << shift);
    return out;
}

void tick(uint32_t now, bool night) {
    uint32_t frame[LED_ROWS * LED_PER_ROW] = {};
    if (!night) {
        Lock l;
        uint32_t level = LED_MAX_BRIGHTNESS * gState.cfg.led / 100;
        // LED 7 breathes once a second while a live arrival is due in 3 min or less.
        float breath = 0.35f + 0.65f * (0.5f + 0.5f * sinf(millis() * 2 * (float)M_PI / 1000));
        for (int r = 0; r < LED_ROWS; r++) {
            const cta::PickedStop& p = gState.cfg.stops[r];
            if (!p.id[0] || !gState.maskTs[r] || now - gState.maskTs[r] > cta::kLedStaleSec) continue;
            uint32_t color = cta::lineColor(p.route);
            for (int k = 0; k < LED_PER_ROW; k++)
                if (gState.masks[r] & (1 << k)) frame[LED_INDEX(r, k)] = scaled(color, level);
            const cta::StopRecord& rec = gState.recs[r];
            bool due = rec.count && rec.arrivals[0].live && rec.arrivals[0].min <= cta::kHypedMaxMin &&
                       now - rec.ts <= cta::kLedStaleSec;
            if (due) frame[LED_INDEX(r, 6)] = scaled(color, (uint32_t)(level * breath));
        }
    }
    if (memcmp(frame, sLast, sizeof frame) == 0) return;
    memcpy(sLast, frame, sizeof frame);
    for (int i = 0; i < LED_ROWS * LED_PER_ROW; i++) sStrip.setPixelColor(i, frame[i]);
    sStrip.show();
}

}  // namespace leds
