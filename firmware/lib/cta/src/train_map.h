#pragma once
// The dock map's L: each line's track and each platform's position, generated into train_map.cpp by
// tools/map_build.py from data/. Coordinates are fixed point, degrees x 1e5.
#include <stddef.h>
#include <stdint.h>

namespace cta {

struct TrainShape {
    const char* route;          // GTFS route_id: Red, Blue, Brn, G, Org, P, Pink, Y
    const int32_t (*pts)[2];    // {lat, lon}, in order along the track
    uint16_t count;
};
extern const TrainShape kTrainShapes[];   // a line may have several runs (Green's branches, Purple's express)
extern const size_t kTrainShapeCount;

struct PlatformPos {
    char id[6];                 // platform stop_id, 3xxxx; sorted
    int32_t lat, lon;           // the station's position
};
extern const PlatformPos kPlatforms[];
extern const size_t kPlatformCount;

// A platform's position in degrees. False (both left untouched) for an unknown or null id.
bool trainPlatformLatLon(const char* stopId, float& lat, float& lon);

}  // namespace cta
