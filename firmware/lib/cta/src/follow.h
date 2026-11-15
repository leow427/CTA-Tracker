#pragma once
// Following one vehicle at a stop (ecosystem spec 5.7): the wearable's Home card counts down to the train or bus the
// app picked, matched by run number or vehicle ID in the predictions the wearable polls for that stop.
#include <stdint.h>

#include "cta_config.h"
#include "cta_types.h"
#include "link_msg.h"

namespace cta {

enum class FollowState : uint8_t { Waiting, Due, Arrived, Lost };   // Arrived and Lost are final

// What the rules remember between polls; start each follow with a fresh FollowMemo{}.
struct FollowMemo {
    FollowState state = FollowState::Waiting;
    bool seen = false;      // the vehicle has been on the card
    int16_t lastMin = 0;    // its minutes on the newest record that had it
    uint32_t seenTs = 0;    // that record's ts
    uint32_t firstTs = 0;   // the later of the first fresh record's ts and now then (fresh ts is always above 0)
    uint32_t lastTs = 0;    // ts of the newest record looked at
    uint32_t endedAt = 0;   // now when it went Arrived or Lost
};

struct FollowView {
    FollowState state;
    int16_t min;   // -1 for a vehicle never on the card (further out than its 4 arrivals)
};

// recs: the wearable's cards. Finds the card with f.stop's type, id, route, and dir (cut to 24 bytes, as cards keep
// it), then the arrival with f.vehicle. Only a record newer than the last one looked at counts, so a failed poll
// (same ts) never ends a follow:
// - on the card: Due at 0 min, else Waiting;
// - seen before and missing from a fresh record: Arrived if its expected minutes (last minutes less the time since)
//   are 1 or less; Waiting if the card is full and it would sort past the last arrival; else Lost. A record whose
//   arrivals carry no vehicle IDs at all can't tell.
// A fresh poll of the stop (another card with the same type and id has a newer ts) without the follow's card counts
// as a fresh record with no arrivals: the parsers make no empty cards.
// Every follow ends: a seen vehicle is Arrived once now passes its last minutes plus kFollowGraceMin (its card gone
// or not), and one never seen is Lost after kFollowUnseenCapMin minutes of fresh records, counted from no earlier
// than the follow's first look. now is Unix time.
FollowView followView(const StopRecord* recs, int n, const FollowMsg& f, FollowMemo& memo, uint32_t now);

// True once Arrived or Lost has lasted kFollowEndedSecs: the card falls back to the pin.
bool followExpired(const FollowMemo& memo, uint32_t now);

}  // namespace cta
