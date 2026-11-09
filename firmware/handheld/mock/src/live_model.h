#pragma once
// The live build: turns the network task's snapshot and the power chip's state into the model the screens draw.
#include "live.h"
#include "mock_data.h"
#include "power.h"

namespace mock {

// Rebuilds every card from s for Unix time now, sets the device state (offline, docked, battery, weather), and
// finishes the model (sorting, mood, reason, home card). Arrivals count down from their fetch time, as the app's
// do: each loses the whole minutes since its record's ts, ones that have passed drop, and a card with none left
// drops. clockKnown is false until NTP syncs (the clock shows --:--).
void fromLive(Model& m, const live::Snapshot& s, const power::State& p, uint32_t now, bool clockKnown);

}  // namespace mock
