#pragma once
// The link messages (ecosystem spec 4.3, 4.4): the JSON bodies of the boards' HTTP API and the BLE setup writes.
// Every parser fills a temporary and copies it to out only on success, so a bad body leaves the old state alone;
// a body over kMaxBody bytes is rejected before it's parsed.
#include <stddef.h>
#include <stdint.h>

#include "ble_frame.h"
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

// True if t is a token: 32 lowercase hex characters.
bool validToken(const char* t);
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
// A device's own − / + (ecosystem spec 5.2): the next 10% step from pct in dir (+1 or -1), within lo–100.
uint8_t stepPct(int pct, int dir, int lo);
// Settings' quiet hours line: "Quiet 10 PM to 7:30 AM · set in the app", or "Quiet hours off" (start == end).
void quietLine(uint16_t start, uint16_t end, char* out, size_t cap);
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
    bool paired;   // has a token; false raises the app's "found" card
    int8_t rssi;   // dBm; 0 when not joined
    bool hasSettings;
    DeviceSettings settings;
    bool relaying;   // a /relay tick came over Bluetooth in the last kRelayHoldSec
};
// {"type":"dock"|"handheld","id","fw","battery","charging","wifi":{"connected","ssid","rssi"},"last_update",
//  "uptime","picks_ver","wake","docked","paired","settings" (config's shape, only with hasSettings),"relaying"}.
// Returns the length (and a NUL), or 0 if it doesn't fit in cap.
constexpr size_t kMaxInfo = 640;   // fits any InfoMsg, even an SSID of 32 control bytes (530 bytes)
size_t encodeInfo(const InfoMsg& in, char* buf, size_t cap);

// --- The wearable's Bluetooth link (wearable BLE link plan, "Envelope", "Messages") ---

// "<POST|DELETE> <path>\n<body>": path starts with /, 2-15 printable bytes, no spaces; the newline is required and
// the body may be empty. body points into msg. False (outputs untouched) otherwise, or if msg is over kMaxBleMsg.
enum class Method : uint8_t { Post, Delete };
constexpr size_t kPathLen = 15;
bool parseEnvelope(const char* msg, size_t len, Method& m, char path[kPathLen + 1], const char*& body,
                   size_t& bodyLen);

// POST /relay {"now","wmo"?,"temp"?}: now a Unix time after 1700000000; wmo (0-99) and temp (C, -100-100) come
// together or not at all (null counts as absent).
struct RelayTick {
    uint32_t now;
    bool hasWeather;
    int wmo;
    float tempC;
};
bool parseRelayTick(const char* json, size_t len, RelayTick& out);

// POST /stop {"id","type","records":[stop record...]}: id 1-7 bytes, type train or bus, records an array (may be
// empty). Each record is read by decodeStopRecord's rules, and its id and type must match the stop's; the first
// kMaxRelayRecords are kept, but every record is checked. Up to kMaxBleMsg bytes (HTTP limits it to kMaxBody).
constexpr int kMaxRelayRecords = 16;   // the wearable's cards per station (live_store.h kCardsPerStation)
struct RelayStop {
    char id[8];
    Kind type;
    StopRecord records[kMaxRelayRecords];
    int n;
};
bool parseRelayStop(const char* json, size_t len, RelayStop& out);

}  // namespace cta
