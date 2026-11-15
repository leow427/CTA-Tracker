#include "follow.h"

#include <string.h>

#include "text.h"

namespace cta {

namespace {

bool ended(const FollowMemo& m) { return m.state == FollowState::Arrived || m.state == FollowState::Lost; }

void end(FollowMemo& m, FollowState state, uint32_t now) {
    m.state = state;
    m.endedAt = now;
}

FollowView viewOf(const FollowMemo& m) { return {m.state, m.seen ? m.lastMin : static_cast<int16_t>(-1)}; }

// The follow's card, or nullptr; stopTs gets the newest ts of any card at the same stop.
const StopRecord* findCard(const StopRecord* recs, int n, const PickedStop& stop, uint32_t& stopTs) {
    char dir[kNameLen + 1];
    copyTruncated(dir, stop.dir, kNameLen);   // a card keeps 24 bytes of a longer direction label
    const StopRecord* found = nullptr;
    stopTs = 0;
    for (int i = 0; i < n; ++i) {
        const StopRecord& r = recs[i];
        if (r.type != stop.type || strcmp(r.id, stop.id) != 0) continue;
        if (r.ts > stopTs) stopTs = r.ts;
        if (!found && strcmp(r.route, stop.route) == 0 && strcmp(r.dir, dir) == 0) found = &r;
    }
    return found;
}

// A fresh record: find the vehicle, or judge where it went.
void look(const StopRecord& r, const char* vehicle, FollowMemo& memo, uint32_t now) {
    memo.lastTs = r.ts;
    if (memo.firstTs == 0) memo.firstTs = r.ts > now ? r.ts : now;   // the unseen window starts no earlier than now
    const uint8_t count = r.count < kMaxArrivals ? r.count : static_cast<uint8_t>(kMaxArrivals);
    bool anyIds = false;
    for (uint8_t i = 0; i < count; ++i) {
        const Arrival& a = r.arrivals[i];
        if (a.vehicle[0] == '\0') continue;
        anyIds = true;
        if (strcmp(a.vehicle, vehicle) == 0) {
            memo.seen = true;
            memo.lastMin = a.min;
            memo.seenTs = r.ts;
            memo.state = a.min == 0 ? FollowState::Due : FollowState::Waiting;
            return;
        }
    }
    if (!memo.seen || (count > 0 && !anyIds)) return;
    const int32_t expected = memo.lastMin - static_cast<int32_t>((r.ts - memo.seenTs) / 60);
    if (expected <= 1)
        end(memo, FollowState::Arrived, now);
    else if (!(count == kMaxArrivals && expected >= r.arrivals[count - 1].min))
        end(memo, FollowState::Lost, now);
    // else it slid off a full card behind sooner arrivals: keep waiting
}

}  // namespace

FollowView followView(const StopRecord* recs, int n, const FollowMsg& f, FollowMemo& memo, uint32_t now) {
    if (ended(memo)) return viewOf(memo);
    uint32_t stopTs;
    const StopRecord* r = findCard(recs, n, f.stop, stopTs);
    if (r != nullptr) {
        if (r->ts > memo.lastTs) look(*r, f.vehicle, memo, now);
    } else if (stopTs > memo.lastTs) {
        // The parsers make no empty cards: a fresh poll of the stop without this line's card means no arrivals.
        StopRecord empty{};
        empty.ts = stopTs;
        look(empty, f.vehicle, memo, now);
    }
    if (ended(memo)) return viewOf(memo);
    if (memo.seen) {
        if (now > memo.seenTs + (static_cast<uint32_t>(memo.lastMin) + kFollowGraceMin) * 60)
            end(memo, FollowState::Arrived, now);
    } else if (memo.firstTs != 0 && memo.lastTs >= memo.firstTs + static_cast<uint32_t>(kFollowUnseenCapMin) * 60) {
        end(memo, FollowState::Lost, now);
    }
    return viewOf(memo);
}

bool followExpired(const FollowMemo& memo, uint32_t now) {
    return ended(memo) && now - memo.endedAt >= kFollowEndedSecs;
}

}  // namespace cta
