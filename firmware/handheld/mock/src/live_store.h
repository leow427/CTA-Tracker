#pragma once
// Everything the live build has polled, and how each response lands in it (spec 6.2, 6.6). Plain C++ with no
// network or Arduino code, so tools/preview.cpp can feed it recorded responses and render what the device would.
// One writer: the network task on the device.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"
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

    struct Staging {   // a response parses here first, so a failed one leaves the last good data
        cta::StopRecord train[kCardsPerStation];
        cta::StopRecord bus[kMaxBusCards];
    };
    cta::Nearby nearby_;
    bool located_ = false;
    Station* stations_ = nullptr;
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
