#pragma once
// The model the screens draw from. The mock fills it with preset Lakeview stations and bus stops around a fixed
// location (Belmont & Sheffield), with arrivals simulated on fixed headways so minutes count down and new trains
// appear. The live build (live_model.cpp) fills it from real CTA data instead; both end in finish().
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"

namespace mock {

constexpr double kMeLat = 41.9386, kMeLon = -87.6515;   // the phone's fix in the mockup
constexpr int kMaxCards = 80;   // per tab: cta::kMaxTrainCards and kMaxBusCards

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
    bool close;            // one of the 3 closest stops of either kind: only these may hype (spec 6.3)
};

struct Model {
    Card trains[kMaxCards];
    int nTrains = 0;
    Card buses[kMaxCards];
    int nBuses = 0;
    Scenario scenario = Scenario::Live;
    Weather weather = Weather::Clear;
    uint32_t now = 0;        // Unix time
    uint32_t fetchedAt = 0;  // Unix time of the last poll (simulated in the mock; the newest record's ts live)
    cta::Mood mood = cta::Mood::Happy;
    bool docked = false;
    // Trackie's reason and the home screen's most urgent card.
    char reason[40] = "";
    const Card* glance = nullptr;
    bool glanceIsBus = false;
    // The device's own state. The live build sets these; the mock keeps the defaults.
    bool live = false;         // real data: no preset weather stands in before Open-Meteo answers
    bool clockKnown = true;    // false until NTP syncs: the clock shows --:--
    bool offline = false;      // no fresh data: no Wi-Fi, or every record has gone stale
    bool loaded = true;        // a first fetch has landed (until then the lists say "Loading")
    char status[40] = "";      // why it's offline, for Trackie's reason ("Connecting to Wi-Fi")
    bool powerDocked = false;  // on USB power with a battery, on the home network (spec 8.4)
    int battery = 64;          // percent; -1 with no battery
    bool charging = true;
    bool hasWeather = false;   // wmo and tempC came from Open-Meteo
    bool weatherForced = false;// the debug gesture picked `weather`, so live weather leaves it alone
    int wmo = 0;
    float tempC = 0;
};

// Sorts the cards (nearest stop first, soonest first within it), then sets the mood, Trackie's reason, and the
// home screen's card from them. Call after filling the cards, `now`, `scenario`, and the device state.
void finish(Model& m);

// The status row's weather: live from Open-Meteo when there is some (and the debug gesture hasn't picked one),
// otherwise the preset for `weather`. In the live build with no weather yet, icon is nullptr: show none.
struct WeatherView {
    const char* icon;
    char temp[8];
    uint32_t rgb;
    int wmo;
    float tempC;
};
WeatherView weatherView(const Model& m);
Weather weatherFor(int wmo);   // Rain for rain and thunder codes, Snow for snow codes, else Clear

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
