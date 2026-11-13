#include "leds.h"

#include <Arduino.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <freertos/queue.h>
#include <string.h>

#include "pins.h"

namespace leds {

namespace {

// The RMT channel refills its 48-symbol buffer from an interrupt every ~30 µs while sending, on the core that
// created it: here core 0, where the panel's bounce-buffer refills (up to a millisecond each, on core
// 1) can't hold it up and garble the colors further down the chain. The loop posts the rows' state through a one-slot
// mailbox (overwritten), so it never waits on the strip.
struct Input {
    cta::LedRow rows[cta::kLedRows];
    uint32_t now;      // Unix time when posted; the task adds the time since
    uint32_t postedMs;
    bool night;
};
QueueHandle_t mailbox;

// Our own RMT channel rather than the Adafruit NeoPixel library's: arduino-esp32 3.2.0 (pinned for the panel, see
// platformio.ini) builds the library's channel config on the stack without setting flags.allow_pd, and the ESP32-S3
// refuses the channel whenever that leftover bit is set, so the LEDs failed at random boots. A zeroed config can't.
rmt_channel_handle_t chan = nullptr;
rmt_encoder_handle_t encoder = nullptr;

bool setUp() {
    rmt_tx_channel_config_t cc = {};
    cc.gpio_num = static_cast<gpio_num_t>(LED_DATA_PIN);
    cc.clk_src = RMT_CLK_SRC_DEFAULT;
    cc.resolution_hz = 10000000;   // 0.1 µs ticks
    cc.mem_block_symbols = 48;     // one block, refilled from the interrupt while sending
    cc.trans_queue_depth = 1;
    rmt_bytes_encoder_config_t ec = {};
    // WS2812B bits at 0.1 µs: a 0 is 0.4 µs high then 0.8 µs low, a 1 is 0.8 µs high then 0.4 µs low.
    ec.bit0.level0 = 1; ec.bit0.duration0 = 4; ec.bit0.level1 = 0; ec.bit0.duration1 = 8;
    ec.bit1.level0 = 1; ec.bit1.duration0 = 8; ec.bit1.level1 = 0; ec.bit1.duration1 = 4;
    ec.flags.msb_first = 1;
    return rmt_new_tx_channel(&cc, &chan) == ESP_OK && rmt_new_bytes_encoder(&ec, &encoder) == ESP_OK &&
           rmt_enable(chan) == ESP_OK;
}

// Sends the colors (0xRRGGBB) in the strip's G, R, B byte order and waits until they're out (about 0.7 ms). The gap to
// the next frame (40 ms or more) is the latch.
void show(const uint32_t* rgb) {
    static uint8_t bytes[cta::kLedCount * 3];
    for (int i = 0; i < cta::kLedCount; ++i) {
        bytes[3 * i] = rgb[i] >> 8;
        bytes[3 * i + 1] = rgb[i] >> 16;
        bytes[3 * i + 2] = rgb[i];
    }
    rmt_transmit_config_t tc = {};
    if (rmt_transmit(chan, encoder, bytes, sizeof(bytes), &tc) == ESP_OK) rmt_tx_wait_all_done(chan, 20);
}

void ledTask(void*) {
    if (!setUp()) {
        Serial.println("leds: RMT channel setup failed");
        vTaskDelete(nullptr);
    }
    static Input in;
    bool have = false;
    uint32_t last[cta::kLedCount] = {}, rgb[cta::kLedCount] = {};
    show(rgb);   // all off
    for (;;) {
        if (xQueueReceive(mailbox, &in, pdMS_TO_TICKS(40)) == pdTRUE) have = true;   // a new state, or 25 Hz
        if (!have) continue;
        const uint32_t ms = millis();
        cta::ledFrame(in.rows, in.now ? in.now + (ms - in.postedMs) / 1000 : 0, in.night, LED_MAX_BRIGHTNESS, ms, rgb);
        if (memcmp(rgb, last, sizeof(rgb)) == 0) continue;
        memcpy(last, rgb, sizeof(rgb));
        show(rgb);   // brightness is already in the colors (cta::ledFrame caps it)
    }
}

}  // namespace

void begin() {
    mailbox = xQueueCreate(1, sizeof(Input));
    // Priority 3: above the network task (2) and the band copier (1), below touch (4).
    xTaskCreatePinnedToCore(ledTask, "leds", 3072, nullptr, 3, nullptr, 0);
}

void post(const cta::LedRow rows[cta::kLedRows], uint32_t now, bool night) {
    if (!mailbox) return;
    static Input in;
    memcpy(in.rows, rows, sizeof(in.rows));
    in.now = now;
    in.postedMs = millis();
    in.night = night;
    xQueueOverwrite(mailbox, &in);
}

}  // namespace leds
