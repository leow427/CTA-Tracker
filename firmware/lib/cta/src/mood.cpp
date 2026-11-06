#include "mood.h"

#include "cta_config.h"

namespace cta {

static bool stale(const StopRecord& r, uint32_t now) {
    return r.ts == 0 || (now > r.ts && now - r.ts > kStaleSec);
}

Mood stopMood(const StopRecord& r, uint32_t now, bool night) {
    if (stale(r, now) || night) return Mood::Sleepy;
    if (r.alert || r.count == 0) return Mood::Sad;
    if (r.arrivals[0].delayed) return Mood::Worried;
    return Mood::Happy;
}

Mood deviceMood(const StopRecord* recs, const bool* eligible, size_t n, uint32_t now, bool night, bool hasLocation) {
    if (!hasLocation || night) return Mood::Sleepy;
    for (size_t i = 0; i < n; i++) {
        const StopRecord& r = recs[i];
        if (eligible[i] && !stale(r, now) && r.count && r.arrivals[0].live && r.arrivals[0].min <= kHypedMaxMin)
            return Mood::Hyped;
    }
    int sad = 0, worried = 0, happy = 0;
    for (size_t i = 0; i < n; i++) {
        switch (stopMood(recs[i], now, false)) {
            case Mood::Sad: sad++; break;
            case Mood::Worried: worried++; break;
            case Mood::Happy: happy++; break;
            default: break;
        }
    }
    if (sad + worried + happy == 0) return Mood::Sleepy;
    if (sad >= worried && sad >= happy) return Mood::Sad;
    if (worried >= happy) return Mood::Worried;
    return Mood::Happy;
}

}  // namespace cta
