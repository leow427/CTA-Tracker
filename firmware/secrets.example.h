#pragma once
// Copy to firmware/secrets.h (git-ignored) and fill in the keys from the CTA Developer Center.
// Never commit secrets.h.
#define CTA_TRAIN_KEY ""
#define CTA_BUS_KEY ""

// Wi-Fi for the live handheld prototype (firmware/handheld/mock, env:live), until the app sets it over BLE.
// The first network is home: on USB power there, the handheld counts as docked (spec 8.4).
#define WIFI_SSID ""
#define WIFI_PASS ""

// Optional: a replacement Wi-Fi MAC, if a router has blocked the board's own. Keep the first byte's second-lowest
// bit set (locally administered, like 0x02) and its lowest bit clear.
// #define WIFI_MAC {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}
