#pragma once
// Internal: keeps the soonest kMaxArrivals arrivals, sorted, as they're added.
#include "cta_types.h"

namespace cta {
struct SoonestArrivals {
    Arrival items[kMaxArrivals];
    int64_t at[kMaxArrivals];   // exact seconds until arrival, for stable sorting
    uint8_t count = 0;
    // Returns the slot the arrival went into, or -1 if it's later than all kept ones.
    int add(int64_t secs, const Arrival& a) {
        int pos = count;
        while (pos > 0 && at[pos - 1] > secs) pos--;
        if (pos >= (int)kMaxArrivals) return -1;
        int last = count < kMaxArrivals ? count : kMaxArrivals - 1;
        for (int i = last; i > pos; i--) { items[i] = items[i - 1]; at[i] = at[i - 1]; }
        items[pos] = a;
        at[pos] = secs;
        if (count < kMaxArrivals) count++;
        return pos;
    }
};
}  // namespace cta
