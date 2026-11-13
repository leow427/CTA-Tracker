#include <string.h>

#include "train_map.h"

namespace cta {

bool trainPlatformLatLon(const char* stopId, float& lat, float& lon) {
    if (stopId == nullptr) return false;
    size_t lo = 0, hi = kPlatformCount;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const int c = strcmp(kPlatforms[mid].id, stopId);
        if (c == 0) {
            lat = kPlatforms[mid].lat / 1e5f;
            lon = kPlatforms[mid].lon / 1e5f;
            return true;
        }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return false;
}

}  // namespace cta
