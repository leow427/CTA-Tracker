#pragma once
// The `config`, `info` and `wifi` BLE messages (base spec 10) and the settings they carry.
#include <stddef.h>
#include <stdint.h>

#include "cta_config.h"
#include "cta_types.h"

namespace cta {

struct PickedStop {   // spec 6.1; id[0] == '\0' means the slot is empty
    Kind type;
    char route[kIdLen + 1];
    char dir[kNameLen + 1];
    char id[kIdLen + 1];
    char name[kNameLen + 1];
};

struct DeviceConfig {
    PickedStop stops[3];   // dock only
    char petName[kPetNameLen + 1];
    uint32_t petColors[2];   // 0xRRGGBB for the pet's body and its light shade (palette slots 2, 3)
    uint8_t screen;          // brightness, percent
    uint8_t led;             // brightness, percent (dock)
    int16_t nightStart;      // minutes after midnight
    int16_t nightEnd;
};

DeviceConfig defaultConfig();

// Partial update: only keys present change. All-or-nothing: on any invalid value (brightness over 100,
// night minutes outside 0-1439, a stop without a valid type or id, a color over 0xFFFFFF) cfg is untouched
// and the result is false. A `stops` array replaces all 3 slots; missing or id-less entries become empty.
bool applyConfigJson(const char* json, size_t len, DeviceConfig& cfg);
size_t encodeConfigJson(const DeviceConfig& cfg, bool includeStops, char* buf, size_t cap);
// battery < 0 writes null (the dock has none). ssid may be null or "".
size_t encodeInfoJson(const char* type, const char* fw, int battery, bool charging, bool wifiUp, const char* ssid,
                      uint32_t lastUpdate, char* buf, size_t cap);
// {slot: 0-2, ssid, pass}. Rejects (never truncates) an ssid over 32 bytes or a password over 63.
constexpr size_t kSsidMax = 32;
constexpr size_t kPassMax = 63;
bool decodeWifiJson(const char* json, size_t len, uint8_t& slot, char* ssid, size_t ssidCap, char* pass,
                    size_t passCap);

}  // namespace cta
