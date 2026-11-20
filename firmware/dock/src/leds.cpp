#include "leds.h"
#include "serial_out.h"

#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clock_ms.h"
#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <freertos/queue.h>
#include <string.h>

#include "net.h"
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
// post()'s latest Input for last(), which runs on the httpd task: a copy behind a spinlock, held for a ~60-byte memcpy.
portMUX_TYPE lastMux = portMUX_INITIALIZER_UNLOCKED;
Input lastIn;
bool posted = false;
// Test (no scope): true makes the data pin plain GPIO, 5 s at 3.3 V then 5 s at 0 V with what the pad reads on serial,
// and sends no frames, so a meter can trace the line to the route board (Leo 2026-11-19).
constexpr bool kPinTest = false;

// Our own RMT channel with a zeroed config: a channel config built on the stack with a leftover flags.allow_pd bit
// is refused by the ESP32-S3, which made the LEDs fail at random boots under the old NeoPixel library.
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
    // WS2812B bits at 0.1 µs: a 0 is 0.3 µs high then 0.9 µs low, a 1 is 0.8 µs high then 0.4 µs low. The route board's
    // WS2812B-B-V6 is a newer part: its 0 must be high 0.22–0.38 µs (older WS2812B: 0.25–0.55), so 0.4 was too long.
    ec.bit0.level0 = 1; ec.bit0.duration0 = 3; ec.bit0.level1 = 0; ec.bit0.duration1 = 9;
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
    if (kPinTest) {   // plain GPIO, no RMT: 5 s at 3.3 V, 5 s at 0 V, with what the pad itself reads
        const gpio_num_t pin = static_cast<gpio_num_t>(LED_DATA_PIN);
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
        for (int high = 1;; high ^= 1) {
            gpio_set_level(pin, high);
            vTaskDelay(pdMS_TO_TICKS(10));
            uprintf("leds test: GPIO %d set %d, reads %d\n", LED_DATA_PIN, high, gpio_get_level(pin));
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    if (!setUp()) {
        uprintf("leds: RMT channel setup failed\n");
        vTaskDelete(nullptr);
    }
    static Input in;
    bool have = false;
    uint32_t last[cta::kLedCount] = {}, rgb[cta::kLedCount] = {};
    show(rgb);   // all off
    for (;;) {
        if (xQueueReceive(mailbox, &in, pdMS_TO_TICKS(40)) == pdTRUE) have = true;   // a new state, or 25 Hz
        if (!have) continue;
        const uint32_t ms = nowMs();
        cta::ledFrame(in.rows, in.now ? in.now + (ms - in.postedMs) / 1000 : 0, in.night, LED_MAX_BRIGHTNESS, ms, rgb);
        if (memcmp(rgb, last, sizeof(rgb)) == 0 && !net::fake()) continue;   // fake data: send every tick (data pin tests)
        memcpy(last, rgb, sizeof(rgb));
        show(rgb);   // brightness is already in the colors (cta::ledFrame caps it)
    }
}

}  // namespace

void begin() {
    mailbox = xQueueCreate(1, sizeof(Input));
    // Priority 2: above the network task (1), below touch (3).
    xTaskCreatePinnedToCore(ledTask, "leds", 3072, nullptr, 2, nullptr, 0);
}

void post(const cta::LedRow rows[cta::kLedRows], uint32_t now, bool night) {
    if (!mailbox) return;
    static Input in;
    memcpy(in.rows, rows, sizeof(in.rows));
    in.now = now;
    in.postedMs = nowMs();
    in.night = night;
    xQueueOverwrite(mailbox, &in);
    portENTER_CRITICAL(&lastMux);
    lastIn = in;
    posted = true;
    portEXIT_CRITICAL(&lastMux);
}

bool last(cta::LedRow rows[cta::kLedRows], uint32_t& now, bool& night) {
    portENTER_CRITICAL(&lastMux);
    const Input in = lastIn;
    const bool ok = posted;
    portEXIT_CRITICAL(&lastMux);
    if (!ok) return false;
    memcpy(rows, in.rows, sizeof(in.rows));
    now = in.now ? in.now + (nowMs() - in.postedMs) / 1000 : 0;
    night = in.night;
    return true;
}

}  // namespace leds
