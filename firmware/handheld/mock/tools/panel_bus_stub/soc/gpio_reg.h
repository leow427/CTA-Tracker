#pragma once
// Stand-in for ESP-IDF's soc/gpio_reg.h, for tools/panel_bus_test.cpp (the ESP32-S3's addresses).
#define GPIO_OUT_W1TS_REG 0x60004008u
#define GPIO_OUT_W1TC_REG 0x6000400Cu
#define GPIO_OUT1_W1TS_REG 0x60004014u
#define GPIO_OUT1_W1TC_REG 0x60004018u
