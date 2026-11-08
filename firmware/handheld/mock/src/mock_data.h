#pragma once
// Preset nearby stops for the mock: the handheld mockup's Lakeview stations and bus stops around a fixed location
// (Belmont & Sheffield), with arrivals simulated on fixed headways so minutes count down and new trains appear.
// It stands in for the BLE nearby list and the Wi-Fi polls; the screens read only this.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"

namespace mock {

constexpr double kMeLat = 41.9386, kMeLon = -87.6515;   // the phone's fix in the mockup
constexpr int kMaxCards = 40;

// Debug states, cycled by a long press on Trackie (or two fingers anywhere). Live computes the mood from the data,
// as the device will; the rest force one state and nudge the data to match it, like the mockup's panel.
enum class Scenario : uint8_t { Live, Happy, Hyped, Worried, Sad, Sleepy, Offline, Docked, FirstRun, Pairing, Count };
const char* scenarioName(Scenario s);

enum class Weather : uint8_t { Clear, Rain, Snow, Count };

struct Card {
    cta::StopRecord rec;   // id, type, route, dir (where it ends), name (the stop), arrivals, alert, ts
    uint8_t station;       // index into the station or bus-stop table
    const char* pips[3];   // a train station's other lines
    uint8_t nPips;
    bool sched;            // timetable only: shown as a clock time
    int delayMin;          // how late the next arrival runs (Worried)
    double lat, lon;
    float distKm;
    char heading[32];      // "To Kimball", "West to Harlem"
};

struct Model {
    Card trains[kMaxCards];
    int nTrains = 0;
    Card buses[kMaxCards];
    int nBuses = 0;
    Scenario scenario = Scenario::Live;
    Weather weather = Weather::Clear;
    uint32_t now = 0;        // Unix time
    uint32_t fetchedAt = 0;  // Unix time of the last simulated poll
    cta::Mood mood = cta::Mood::Happy;
    bool docked = false;
    // Trackie's reason and the home screen's most urgent card.
    char reason[40] = "";
    const Card* glance = nullptr;
    bool glanceIsBus = false;
};

// Rebuilds every card for Unix time `now` (simulated arrivals, the scenario's tweaks, sorting, mood, reason).
// startUnix is when the mock started; arrivals are laid out relative to it.
void update(Model& m, uint32_t now, uint32_t startUnix);

// Minutes after local midnight for a Unix time, in America/Chicago (CST/CDT by the US rules).
int localMinutes(uint32_t unix);
// "8:41" (12-hour, no AM/PM) for unix + offsetMin minutes.
void clockText(uint32_t unix, int offsetMin, char* out, size_t cap);

const char* lineName(const char* route);   // "Brn" → "Brown"; bus routes come back as is
void weatherInfo(Weather w, const char*& icon, const char*& temp, uint32_t& rgb, int& wmo, float& tempC);

}  // namespace mock
