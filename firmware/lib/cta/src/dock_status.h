#pragma once
// What the dock shows about its stops between fetches (spec 6.3, 6.4, 7.4): arrivals counted down from the fetch,
// whether the next one is due (LED 7 and Hyped), whether a card is greyed out, Trackie's mood, and the reason he
// gives for it when tapped. A record's route and type say which line it is.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"

namespace cta {

// Arrival k's minutes less the whole minutes since the fetch (r.ts), never below 0.
int minutesLeft(const StopRecord& r, int k, uint32_t now);
// The next arrival is live and within kHypedMaxMin, counted down, and the record is fresh (not isStale).
bool dueNow(const StopRecord& r, uint32_t now);
// A fetched record is greyed out while Wi-Fi is lost, or once it's stale.
bool cardStale(const StopRecord& r, uint32_t now, bool wifiLost);
// deviceMood over the dock's records, all Hyped-eligible, with the arrivals counted down to now.
Mood dockMood(const StopRecord* recs, size_t n, uint32_t now, bool night);
// "Red", "Brown", …; a bus route as it is.
const char* trainLineName(const char* route);
// Trackie's reason: "Data is N min old" while Wi-Fi is lost, "Night hours", the network status before any data,
// else the first stop that explains the mood ("Red in 3 min!", "Red Line alert", "22 is running late", "Red in 6 min").
void moodReason(const StopRecord* recs, size_t n, uint32_t now, bool night, bool wifiLost, const char* status, char* out,
                size_t cap);

}  // namespace cta
