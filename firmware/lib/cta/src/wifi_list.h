#pragma once
// The wearable's saved Wi-Fi networks (ecosystem spec 5.1, todo F1): up to kWifiSlots, slot 0 home. It joins the
// strongest saved network in range, and moves to the next strongest when the link drops.
#include <stdint.h>

#include "link_msg.h"

namespace cta {

struct SavedNet {
    char ssid[kSsidLen + 1];
    char pass[kPassLen + 1];
};
struct WifiList {
    SavedNet nets[kWifiSlots];   // an empty ssid is a free slot
};
struct SeenNet {
    const char* ssid;
    int8_t rssi;
};

// The saved slot whose network is seen with the strongest signal, skipping slots whose bit is set in skipMask
// (ones that just failed); a tie goes to the lower slot. -1 if none is in range.
int strongestSaved(const WifiList& list, const SeenNet* seen, int nSeen, uint8_t skipMask);

}  // namespace cta
