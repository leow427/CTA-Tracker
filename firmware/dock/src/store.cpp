#include "store.h"

#include <Preferences.h>

namespace store {

static Preferences sPrefs;
static constexpr uint32_t kConfigVersion = 1;   // bump when DeviceConfig's layout changes

void begin() { sPrefs.begin("dock", false); }

cta::DeviceConfig loadConfig() {
    cta::DeviceConfig cfg = cta::defaultConfig();
    if (sPrefs.getUInt("cfgv", 0) == kConfigVersion && sPrefs.getBytesLength("cfg") == sizeof cfg) {
        sPrefs.getBytes("cfg", &cfg, sizeof cfg);
        for (auto& s : cfg.stops) {   // never trust flash with unterminated strings
            s.id[sizeof s.id - 1] = s.route[sizeof s.route - 1] = '\0';
            s.dir[sizeof s.dir - 1] = s.name[sizeof s.name - 1] = '\0';
        }
        cfg.petName[sizeof cfg.petName - 1] = '\0';
    }
    return cfg;
}

void saveConfig(const cta::DeviceConfig& cfg) {
    sPrefs.putBytes("cfg", &cfg, sizeof cfg);
    sPrefs.putUInt("cfgv", kConfigVersion);
}

bool loadWifi(char* ssid, size_t ssidCap, char* pass, size_t passCap) {
    if (!sPrefs.isKey("ssid")) return false;
    sPrefs.getString("ssid", ssid, ssidCap);
    sPrefs.getString("pass", pass, passCap);
    return ssid[0] != '\0';
}

void saveWifi(const char* ssid, const char* pass) {
    sPrefs.putString("ssid", ssid);
    sPrefs.putString("pass", pass);
}

bool loadGeo(double& lat, double& lon) {
    if (!sPrefs.isKey("lat")) return false;
    lat = sPrefs.getDouble("lat");
    lon = sPrefs.getDouble("lon");
    return true;
}

void saveGeo(double lat, double lon) {
    sPrefs.putDouble("lat", lat);
    sPrefs.putDouble("lon", lon);
}

}  // namespace store
