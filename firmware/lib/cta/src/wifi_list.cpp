#include "wifi_list.h"

#include <string.h>

namespace cta {

int strongestSaved(const WifiList& list, const SeenNet* seen, int nSeen, uint8_t skipMask) {
    int best = -1, bestRssi = 0;
    for (int slot = 0; slot < kWifiSlots; ++slot) {
        const char* ssid = list.nets[slot].ssid;
        if (!ssid[0] || (skipMask & (1u << slot))) continue;
        for (int i = 0; i < nSeen; ++i) {
            if (seen[i].ssid == nullptr || strcmp(seen[i].ssid, ssid) != 0) continue;
            if (best < 0 || seen[i].rssi > bestRssi) {
                best = slot;
                bestRssi = seen[i].rssi;
            }
        }
    }
    return best;
}

// Codes from ESP-IDF 5.4's Wi-Fi guide, "Wi-Fi Reason Code" and "Wi-Fi Reason code related to wrong password":
// https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/api-guides/wifi.html
WifiFail wifiFailFor(uint16_t reason) {
    switch (reason) {
        case 0: return WifiFail::None;
        case 15:    // 4WAY_HANDSHAKE_TIMEOUT
        case 204:   // HANDSHAKE_TIMEOUT: the guide's wrong-password codes (202 AUTH_FAIL isn't one: it's Other)
            return WifiFail::Password;
        case 201: return WifiFail::NoNetwork;   // NO_AP_FOUND
        case 2:     // AUTH_EXPIRE: a lab access point's device allow-list sent it, but an auth timeout at the edge of range does too
        case 3:     // AUTH_LEAVE
        case 9:     // ASSOC_NOT_AUTHED
        case 30:    // NOT_AUTHORIZED_THIS_LOCATION
        case 203:   // ASSOC_FAIL
            return WifiFail::Refused;
        default: return WifiFail::Other;
    }
}

}  // namespace cta
