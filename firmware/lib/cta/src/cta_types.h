#pragma once
// Shared data shapes for the dock, the handheld, and their tests (spec 6.2). Mirrored in the app's CTAKit.
#include <stddef.h>
#include <stdint.h>

namespace cta {

constexpr size_t kMaxArrivals = 4;
constexpr size_t kNameLen = 24;  // bytes, excluding NUL

enum class Kind : uint8_t { Train = 0, Bus = 1 };
enum class Mood : uint8_t { Sleepy, Sad, Worried, Happy, Hyped };
enum class Accessory : uint8_t { None, Umbrella, Scarf };

struct Arrival {
    int16_t min;
    char dest[kNameLen + 1];
    bool live;
    bool delayed;
};

struct StopRecord {
    char id[8];
    Kind type;
    char route[8];
    char dir[kNameLen + 1];
    char name[kNameLen + 1];
    Arrival arrivals[kMaxArrivals];
    uint8_t count;
    bool alert;
    uint32_t ts;  // Unix time of the fetch, 0 = never
};

struct TrainPos {
    char nextStpId[8];
};

struct BusVehicle {
    int32_t pid;
    int32_t pdist;
};

// One dock LED row (spec 6.4): LEDs 0–5 are the stops before the picked stop, farthest first; LED 6 is the stop.
struct ApproachRow {
    char stops[7][8];  // stops[k][0] == '\0' → no stop at LED k
    int32_t pdist[7];
    int32_t before;
    int32_t pid;
    float lat[7], lon[7];  // where each stop is, for the dock's map; 0, 0 when unknown
};

}  // namespace cta
