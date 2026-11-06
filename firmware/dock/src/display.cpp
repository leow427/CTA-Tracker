#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

#include "pins.h"

namespace display {

static constexpr int kPanelW = 800, kPanelH = 480;
static constexpr uint8_t kGt911 = 0x5D;
static constexpr uint8_t kCh422Mode = 0x24, kCh422Out = 0x38;   // CH422G command "addresses"
static constexpr int kBufLines = 40;

static esp_lcd_panel_handle_t sPanel;
static uint16_t* sFb;
static uint8_t sExio = 0;
static volatile int sTouches = 0;

static void exioWrite(int pin, bool high) {
    sExio = high ? (sExio | (1 << pin)) : (sExio & ~(1 << pin));
    Wire.beginTransmission(kCh422Out);
    Wire.write(sExio);
    Wire.endTransmission();
}

void backlight(bool on) { exioWrite(EXIO_BACKLIGHT, on); }

static bool gtRead(uint16_t reg, uint8_t* out, size_t n) {
    Wire.beginTransmission(kGt911);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)kGt911, (int)n) != (int)n) return false;
    for (size_t i = 0; i < n; i++) out[i] = Wire.read();
    return true;
}

static void gtClear() {
    Wire.beginTransmission(kGt911);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0);
    Wire.endTransmission();
}

// Panel coordinates (landscape) to the portrait UI and back.
static inline void toPortrait(int px, int py, int& x, int& y) {
#if DISPLAY_ROTATE_CW
    x = kWidth - 1 - py;
    y = px;
#else
    x = py;
    y = kHeight - 1 - px;
#endif
}

static void flush(lv_display_t* disp, const lv_area_t* a, uint8_t* pxMap) {
    const uint16_t* src = (const uint16_t*)pxMap;
    for (int y = a->y1; y <= a->y2; y++) {
        for (int x = a->x1; x <= a->x2; x++) {
#if DISPLAY_ROTATE_CW
            int X = y, Y = kWidth - 1 - x;
#else
            int X = kHeight - 1 - y, Y = x;
#endif
            sFb[Y * kPanelW + X] = *src++;
        }
    }
    lv_display_flush_ready(disp);
}

// The GT911 sets "data ready" once per scan. Between scans (or on a failed read) the last report stands, so a
// held finger never reads as a release and long-presses aren't cut short.
static void readTouch(lv_indev_t*, lv_indev_data_t* data) {
    static lv_indev_state_t sState = LV_INDEV_STATE_RELEASED;
    static lv_point_t sPoint = {0, 0};
    uint8_t status;
    if (gtRead(0x814E, &status, 1) && (status & 0x80)) {
        int n = status & 0x0F;
        uint8_t p[8];
        if (n > 0 && n <= 5 && gtRead(0x814F, p, 8)) {
            int x, y;
            toPortrait(p[1] | (p[2] << 8), p[3] | (p[4] << 8), x, y);
            sPoint.x = x < 0 ? 0 : x >= kWidth ? kWidth - 1 : x;
            sPoint.y = y < 0 ? 0 : y >= kHeight ? kHeight - 1 : y;
            sState = LV_INDEV_STATE_PRESSED;
            sTouches = n;
        } else if (n == 0) {
            sState = LV_INDEV_STATE_RELEASED;
            sTouches = 0;
        }
        gtClear();
    }
    data->point = sPoint;
    data->state = sState;
}

int touchCount() { return sTouches; }

static uint32_t tickMs() { return millis(); }

bool begin() {
    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    Wire.beginTransmission(kCh422Mode);
    Wire.write(0x01);   // IO0-7 as outputs
    Wire.endTransmission();
    // GT911 reset with INT low selects address 0x5D.
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);
    sExio = (1 << EXIO_LCD_RST) | (1 << EXIO_SD_CS);
    exioWrite(EXIO_TOUCH_RST, false);
    delay(20);
    exioWrite(EXIO_TOUCH_RST, true);
    delay(60);
    pinMode(TOUCH_INT, INPUT);
    delay(50);

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = LCD_PCLK_HZ;
    cfg.timings.h_res = kPanelW;
    cfg.timings.v_res = kPanelH;
    cfg.timings.hsync_pulse_width = 4;
    cfg.timings.hsync_back_porch = 8;
    cfg.timings.hsync_front_porch = 8;
    cfg.timings.vsync_pulse_width = 4;
    cfg.timings.vsync_back_porch = 8;
    cfg.timings.vsync_front_porch = 8;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 1;
    cfg.bounce_buffer_size_px = kPanelW * 10;   // DMA from internal RAM so Wi-Fi's PSRAM traffic can't tear the panel
    cfg.dma_burst_size = 64;
    cfg.hsync_gpio_num = LCD_HSYNC;
    cfg.vsync_gpio_num = LCD_VSYNC;
    cfg.de_gpio_num = LCD_DE;
    cfg.pclk_gpio_num = LCD_PCLK;
    cfg.disp_gpio_num = -1;
    const int data[16] = LCD_DATA_PINS;
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
    cfg.flags.fb_in_psram = 1;
    if (esp_lcd_new_rgb_panel(&cfg, &sPanel) != ESP_OK || esp_lcd_panel_reset(sPanel) != ESP_OK ||
        esp_lcd_panel_init(sPanel) != ESP_OK) {
        Serial.println("display: RGB panel init failed");
        return false;
    }
    void* fb = nullptr;
    esp_lcd_rgb_panel_get_frame_buffer(sPanel, 1, &fb);
    sFb = (uint16_t*)fb;
    memset(sFb, 0, kPanelW * kPanelH * 2);
    backlight(true);

    lv_init();
    lv_tick_set_cb(tickMs);
    lv_display_t* disp = lv_display_create(kWidth, kHeight);
    size_t bufBytes = kWidth * kBufLines * 2;
    // One buffer: flush() is a synchronous CPU copy, so a second would only cost 38 KB of internal RAM.
    void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        Serial.println("display: no RAM for the draw buffer");
        return false;
    }
    lv_display_set_buffers(disp, buf, nullptr, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);
    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, readTouch);
    uint8_t id[4] = {};
    if (!gtRead(0x8140, id, 4)) Serial.println("display: GT911 not answering");
    return true;
}

void tick() { lv_timer_handler(); }

}  // namespace display
