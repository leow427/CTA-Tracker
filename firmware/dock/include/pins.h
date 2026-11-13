#pragma once
// Waveshare ESP32-S3-Touch-LCD-4.3 wiring (from Waveshare's board demo) and the dock's own hardware (spec 3).

// RGB panel (ST7262, 800 × 480, RGB565 over 16 data lines)
#define LCD_PCLK 7
#define LCD_HSYNC 46
#define LCD_VSYNC 3
#define LCD_DE 5
// B3..B7, G2..G7, R3..R7
#define LCD_DATA_PINS {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40}
// 14 MHz (Waveshare's demo: 16): about 34 frames a second, and an eighth less PSRAM read by the panel's DMA, which
// with Wi-Fi running at 16 MHz fell behind now and then.
#define LCD_PCLK_HZ (14 * 1000 * 1000)

// The I2C bus shared by the GT911 touch controller and the CH422G IO expander
#define I2C_SDA 8
#define I2C_SCL 9
#define TOUCH_INT 4

// CH422G outputs (EXIO1..5)
#define EXIO_TOUCH_RST 1
#define EXIO_BACKLIGHT 2
#define EXIO_LCD_RST 3
#define EXIO_SD_CS 4
#define EXIO_USB_SEL 5

// The dock is used in portrait. 1: the panel's top edge (its long side with the FPC at the top of the landscape
// picture) faces right; 0: it faces left. Flip it if the UI is upside down; touch follows.
#define DISPLAY_ROTATE_CW 1

// Route board: 3 rows × 7 NeoPixels through an SN74AHCT125 level shifter, on the header pin labelled "AD" (Leo,
// 2026-11-12). One chain: row r, position i is index 7r + i (spec 3; cta::ledIndex).
#define LED_DATA_PIN 6
// Hard cap on NeoPixel brightness (0–255), so 21 LEDs stay well inside the 5 V, 3 A budget (spec 3).
#define LED_MAX_BRIGHTNESS 96
