#pragma once
// Everything the live build has polled, and how each response lands in it (spec 6.2, 6.6). Plain C++ with no
// network or Arduino code, so tools/preview.cpp can feed it recorded responses and render what the device would.
// One writer: the network task on the device.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"
#include "link_msg.h"
#include "live.h"
#include "nearby_msg.h"

namespace live {

class Store {
public:
    static constexpr int kCardsPerStation = 16;  // Clark/Lake has 12 (six lines, both ways)
    static constexpr int kMaxRoutes = 48;        // State & Lake's stops and stations name about 34

    // Allocates the record tables and the parsers' staging on the heap: each is over the 4 KB threshold above
    // which the device's malloc puts a block in PSRAM, leaving internal RAM for Wi-Fi and TLS.
    bool init();
    // The stops to keep data for. Drops everything polled so far: it was for other stops.
    void setNearby(const cta::Nearby& n);
    const cta::Nearby& nearby() const { return nearby_; }
    bool located() const { return located_; }

    // Each takes one response body. They return what they parsed (cards, alerted routes) or -1 on an error, in
    // which case the data from the last good response stays.
    int applyTrains(int station, const char* body, size_t len, uint32_t now);   // station: index into trains
    int applyBuses(const char* body, size_t len, uint32_t now);                 // all the bus stops, one call
    int applyAlerts(const char* body, size_t len);                              // for routes()
    bool applyWeather(const char* body, size_t len);
    // A stop's records relayed from the phone (wearable BLE link plan, "Relay"): they replace that stop's cards where
    // its poll would put them (a listed station's slot, that bus stop's cards, the pinned station's own poll) when
    // their newest ts is at least the stored newest; an older relay changes nothing. An empty one (nothing coming, no
    // ts) clears the stop, as an empty poll does. Alert flags are OR'd with the alerts poll. False if the stop is neither listed nor pinned (ignored).
    bool applyRelay(const cta::RelayStop& s);
    // The phone's weather (POST /relay), as an Open-Meteo reply would set it.
    void setWeather(int wmo, float tempC);

    // The pinned stop and the followed vehicle (ecosystem spec 5.6, 5.7). setPin(nullptr) clears both; a pin at
    // another stop drops the old stop's data and ends the follow. setFollow also pins f->stop;
    // updateFollow clears both once the follow has ended (Arrived or Lost) for kFollowEndedSecs.
    void setPin(const cta::PickedStop* p);
    void setFollow(const cta::FollowMsg* f);
    const cta::PickedStop* pin() const { return hasPin_ ? &pin_ : nullptr; }
    // A response for a pinned station outside the list (-1 for a bus pin: its stop rides in applyBuses' poll).
    int applyPin(const char* body, size_t len, uint32_t now);
    // Runs the follow rules on the newest records at the pinned stop; ends the follow once it has expired.
    void updateFollow(uint32_t now);

    // The routes seen in any record so far, for the alerts call.
    int routes(const char** out, int max) const;
    bool everyStopPolled() const;
    // Fills the data fields of s (cards, alerts merged in, weather, loaded, the close stops); leaves the network
    // fields alone.
    void fill(Snapshot& s) const;

private:
    struct Station {
        cta::StopRecord cards[kCardsPerStation];
        int n;
        bool ok;
    };
    void addRoute(const char* route);
    bool isAlerted(const char* route) const;
    // Every record at the pinned stop, from its own poll and the nearby list's, one per line and direction (the
    // newer); written to staging_->atPin.
    int pinRecords() const;

    struct Staging {   // a response parses here first, so a failed one leaves the last good data
        cta::StopRecord train[kCardsPerStation];
        cta::StopRecord bus[kMaxBusCards];
        cta::StopRecord atPin[kCardsPerStation + kMaxBusCards];
    };
    cta::Nearby nearby_;
    bool located_ = false;
    Station* stations_ = nullptr;   // the nearby stations, then the pinned stop's own poll
    cta::PickedStop pin_;
    bool hasPin_ = false;
    cta::FollowMsg follow_;
    cta::FollowMemo memo_;
    cta::FollowView view_ = {};
    bool hasFollow_ = false;
    Staging* staging_ = nullptr;
    cta::StopRecord* bus_ = nullptr;
    int nBus_ = 0;
    bool busOk_ = false;
    char routes_[kMaxRoutes][8] = {};
    bool alerted_[kMaxRoutes] = {};
    int nRoutes_ = 0;
    bool hasWeather_ = false;
    int wmo_ = 0;
    float tempC_ = 0;
};

}  // namespace live
