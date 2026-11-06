#pragma once
// Flash (NVS) storage: the device config, the one dock Wi-Fi network, and the cached weather location.
#include "device_config.h"

namespace store {
void begin();
cta::DeviceConfig loadConfig();
void saveConfig(const cta::DeviceConfig& cfg);
bool loadWifi(char* ssid, size_t ssidCap, char* pass, size_t passCap);   // false when none saved
void saveWifi(const char* ssid, const char* pass);
bool loadGeo(double& lat, double& lon);
void saveGeo(double lat, double lon);
}  // namespace store
