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

}  // namespace cta
