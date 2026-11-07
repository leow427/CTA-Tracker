#include "mood.h"

#include "cta_config.h"

namespace cta {

bool isStale(const StopRecord& r, uint32_t now) {
    return r.ts == 0 || static_cast<int64_t>(now) - static_cast<int64_t>(r.ts) > static_cast<int64_t>(kStaleSec);
}

Mood stopMood(const StopRecord& r, uint32_t now, bool night) {
    if (isStale(r, now) || night) return Mood::Sleepy;
    if (r.alert || r.count == 0) return Mood::Sad;
    if (r.arrivals[0].delayed) return Mood::Worried;
    return Mood::Happy;
}

Mood deviceMood(const StopRecord* recs, const bool* hypedEligible, size_t n, uint32_t now, bool night,
                bool hasLocation) {
    if (!hasLocation || night) return Mood::Sleepy;
    for (size_t i = 0; i < n; ++i) {
        const StopRecord& r = recs[i];
        if (hypedEligible != nullptr && hypedEligible[i] && !isStale(r, now) && r.count > 0 && r.arrivals[0].live &&
            r.arrivals[0].min <= kHypedMaxMin)
            return Mood::Hyped;
    }
    size_t sad = 0, worried = 0, happy = 0;
    for (size_t i = 0; i < n; ++i) {
        switch (stopMood(recs[i], now, false)) {
            case Mood::Sad: ++sad; break;
            case Mood::Worried: ++worried; break;
            case Mood::Happy: ++happy; break;
            default: break;
        }
    }
    if (sad + worried + happy == 0) return Mood::Sleepy;
    // Majority; ties go to the more serious mood.
    if (sad >= worried && sad >= happy) return Mood::Sad;
    if (worried >= happy) return Mood::Worried;
    return Mood::Happy;
}

}  // namespace cta
