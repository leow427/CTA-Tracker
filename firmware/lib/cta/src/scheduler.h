#pragma once
// "What's due now", pure so it's natively testable. `now` is monotonic seconds (uptime), not wall time.
#include <stdint.h>

#include "cta_config.h"

namespace cta {
struct Job {
    uint32_t intervalSec;
    uint32_t lastRun = 0;
    bool ran = false;
    // The first call is always due. Slow mode (night hours) stretches every job to at least kSlowPollSec.
    bool isDue(uint32_t now, bool slow) const {
        if (!ran) return true;
        uint32_t every = slow && intervalSec < kSlowPollSec ? kSlowPollSec : intervalSec;
        return now - lastRun >= every;
    }
    void markRun(uint32_t now) { lastRun = now; ran = true; }
    void reset() { ran = false; }   // run on the next check (a stop changed)
};
}  // namespace cta
