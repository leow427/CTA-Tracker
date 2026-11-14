#pragma once
// The link messages (ecosystem spec 4.3, 4.4): the JSON bodies of the boards' HTTP API and the BLE setup writes.
// Every parser fills a temporary and copies it to out only on success, so a bad body leaves the old state alone;
// a body over kMaxBody bytes is rejected before it's parsed.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"
#include "picks.h"

namespace cta {

constexpr int kWifiSlots = 5;   // the wearable's saved networks, slot 0 home; the dock keeps 1
constexpr int kSsidLen = 32, kPassLen = 64, kTokenLen = 32, kPetNameLen = 16;   // kVehicleLen is in cta_types.h
constexpr size_t kMaxBody = 1024;

// {"slot","ssid","pass"}: slot below maxSlots, ssid 1–32 bytes, pass 0–64 bytes (empty for an open network).
struct WifiMsg {
    uint8_t slot;
    char ssid[kSsidLen + 1];
    char pass[kPassLen + 1];
};
bool parseWifi(const char* json, size_t len, uint8_t maxSlots, WifiMsg& out);

// {"token":"<32 lowercase hex>"}.
bool parseToken(const char* json, size_t len, char out[kTokenLen + 1]);
// True if authHeader is exactly "Bearer <token>" and token is a 32-character token. Constant time in the token.
bool tokenMatches(const char* authHeader, const char* token);

// The ConfigStop shape {type, route, dir, id, name}: type train or bus, id 1–7 bytes, route at most 7 bytes; dir
// and name are cut to fit. Pin and follow bodies carry a train's station ID (4xxxx), dock stops a platform ID.
bool parseStop(const char* json, size_t len, PickedStop& out);

// The stop's fields plus "vehicle" (a run number or bus vehicle ID, 1–7 bytes) and "dest" (cut to fit).
struct FollowMsg {
    PickedStop stop;
    char vehicle[kVehicleLen + 1];
    char dest[kNameLen + 1];
};
bool parseFollow(const char* json, size_t len, FollowMsg& out);

struct PetConfig {
    char name[kPetNameLen + 1];
    uint32_t colors[2];   // 0xRRGGBB for palette slots 2 and 3
};
struct DeviceSettings {
    uint8_t screen;        // brightness, percent
    uint8_t led;           // brightness, percent (dock only)
    uint16_t nightStart;   // minutes after midnight, 0–1439
    uint16_t nightEnd;
};
struct ConfigMsg {
    bool hasStops = false, hasPet = false, hasSettings = false;
    PickedStop stops[kDockStops];
    PetConfig pet;
    DeviceSettings settings;
};
// {stops, pet, settings}, any subset. stops is decodePicks's list; pet {name, colors:[2]}; settings needs all of
// screen, led, night_start, night_end. A key that's there but bad fails the whole message.
bool parseConfig(const char* json, size_t len, ConfigMsg& out);
// Only the has* parts. Returns the length (and a NUL), or 0 if it doesn't fit in cap.
size_t encodeConfig(const ConfigMsg& in, char* buf, size_t cap);

struct InfoMsg {
    bool dock;
    char id[5];
    char fw[16];
    int8_t battery;   // percent; -1 for none (the dock), sent as null
    bool charging;
    bool wifiConnected;
    char ssid[kSsidLen + 1];
    uint32_t lastUpdate;
    uint32_t uptime;   // seconds since boot: lower than last time means the board rebooted
    uint32_t picksVer;
    uint32_t wake;
    bool docked;
};
// {"type":"dock"|"handheld","id","fw","battery","charging","wifi":{"connected","ssid"},"last_update","uptime",
//  "picks_ver","wake","docked"}. Returns the length (and a NUL), or 0 if it doesn't fit in cap.
size_t encodeInfo(const InfoMsg& in, char* buf, size_t cap);

}  // namespace cta
