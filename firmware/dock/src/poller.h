#pragma once
// The network task: Wi-Fi, NTP, and every CTA / weather fetch on the spec 6.6 schedule. Writes gState.
#include "bus_parse.h"

namespace poller {

void begin();   // starts the task (core 0)

// The stop picker's live Bus Tracker lists, fetched on the network task.
enum class Ask : uint8_t { None, Routes, Directions, Stops };
constexpr int kMaxRoutes = 160, kMaxDirs = 4, kMaxStops = 240;
struct PickerLists {
    Ask ask = Ask::None;
    bool done = false, ok = false;
    int count = 0;
    cta::BusRoute routes[kMaxRoutes];
    char dirs[kMaxDirs][cta::kNameLen + 1];
    cta::BusStop stops[kMaxStops];
};
void askBus(Ask ask, const char* route = "", const char* dir = "");
// Copies the finished lists into out (only when out->ask matches the last request and it's done).
bool busLists(PickerLists* out);

}  // namespace poller
