#pragma once
// Shared data shapes (base spec 6.2). Fixed-size, so they live in plain structs on both ESP32s.
#include <stddef.h>
#include <stdint.h>

namespace cta {

constexpr size_t kMaxArrivals = 4;
constexpr size_t kNameLen = 24;   // bytes, excluding NUL (names, dirs, dests)
constexpr size_t kIdLen = 7;      // bytes, excluding NUL (ids, routes)

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
    char id[kIdLen + 1];
    Kind type;
    char route[kIdLen + 1];
    char dir[kNameLen + 1];
    char name[kNameLen + 1];
    Arrival arrivals[kMaxArrivals];
    uint8_t count;
    bool alert;
    uint32_t ts;   // Unix time of the fetch; 0 = never
};

struct TrainPos {
    char nextStpId[kIdLen + 1];
    char route[kIdLen + 1];   // the train's line, as ttpositions names its group ("red", "brn")
};
struct BusVehicle { int32_t pid; int32_t pdist; };

// One LED row: stops[k][0] == '\0' means no stop at LED k. LED 6 is the picked stop.
struct ApproachRow {
    char stops[7][kIdLen + 1];
    int32_t pdist[7];   // buses: distance along the pattern (feet)
    int32_t before;     // buses: pdist of the stop before stops[0], or -1
    int32_t pid;        // buses: pattern id; trains: -1
};

}  // namespace cta
