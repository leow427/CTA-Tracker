#include "live_store.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include "alerts_weather.h"
#include "bus_parse.h"
#include "train_cards.h"

namespace live {

bool Store::init() {
    stations_ = new (std::nothrow) Station[cta::kNearbyPerTab]();
    bus_ = new (std::nothrow) cta::StopRecord[kMaxBusCards]();
    staging_ = new (std::nothrow) Staging();
    return stations_ && bus_ && staging_;
}

void Store::setNearby(const cta::Nearby& n) {
    nearby_ = n;
    located_ = true;
    memset(stations_, 0, sizeof(Station) * cta::kNearbyPerTab);
    nBus_ = 0;
    busOk_ = false;
    nRoutes_ = 0;
    hasWeather_ = false;   // it was for the old spot
}

void Store::addRoute(const char* route) {
    for (int i = 0; i < nRoutes_; ++i)
        if (strcmp(routes_[i], route) == 0) return;
    if (nRoutes_ >= kMaxRoutes) return;
    snprintf(routes_[nRoutes_], sizeof(routes_[0]), "%s", route);
    alerted_[nRoutes_++] = false;
}

bool Store::isAlerted(const char* route) const {
    for (int i = 0; i < nRoutes_; ++i)
        if (strcmp(routes_[i], route) == 0) return alerted_[i];
    return false;
}

int Store::applyTrains(int station, const char* body, size_t len, uint32_t now) {
    if (station < 0 || station >= nearby_.nTrains) return -1;
    cta::StopRecord* cards = staging_->train;
    const int n = cta::parseTrainCards(body, len, now, nearby_.trains[station], cards, kCardsPerStation);
    if (n < 0) return -1;
    Station& s = stations_[station];
    memcpy(s.cards, cards, sizeof(cta::StopRecord) * n);
    s.n = n;
    s.ok = true;
    for (int c = 0; c < n; ++c) addRoute(cards[c].route);
    return n;
}

int Store::applyBuses(const char* body, size_t len, uint32_t now) {
    cta::StopRecord* recs = staging_->bus;
    const int n = cta::parseBusPredictions(body, len, now, nullptr, recs, kMaxBusCards);
    if (n < 0) return -1;
    memcpy(bus_, recs, sizeof(cta::StopRecord) * n);
    nBus_ = n;
    busOk_ = true;
    for (int b = 0; b < n; ++b) addRoute(recs[b].route);
    return n;
}

int Store::routes(const char** out, int max) const {
    int n = 0;
    for (; n < nRoutes_ && n < max; ++n) out[n] = routes_[n];
    return n;
}

int Store::applyAlerts(const char* body, size_t len) {
    const char* wanted[kMaxRoutes];
    const int n = routes(wanted, kMaxRoutes);
    bool on[kMaxRoutes] = {};
    const int k = cta::parseAlertRoutes(body, len, wanted, n, on);
    if (k < 0) return -1;
    memcpy(alerted_, on, sizeof(bool) * n);
    return k;
}

bool Store::applyWeather(const char* body, size_t len) {
    int w;
    float t;
    if (!cta::parseWeather(body, len, w, t)) return false;
    hasWeather_ = true;
    wmo_ = w;
    tempC_ = t;
    return true;
}

bool Store::everyStopPolled() const {
    bool all = located_ && (busOk_ || nearby_.nBuses == 0);
    for (int st = 0; st < nearby_.nTrains; ++st) all = all && stations_[st].ok;
    return all;
}

void Store::fill(Snapshot& s) const {
    s.nTrains = s.nBuses = 0;
    for (int st = 0; st < nearby_.nTrains; ++st)
        for (int c = 0; c < stations_[st].n && s.nTrains < kMaxTrainCards; ++c) {
            Rec& r = s.trains[s.nTrains++];
            r.rec = stations_[st].cards[c];
            r.rec.alert = isAlerted(r.rec.route);
            r.stop = static_cast<uint8_t>(st);
        }
    for (int b = 0; b < nBus_ && s.nBuses < kMaxBusCards; ++b) {
        int stop = 0;
        while (stop < nearby_.nBuses && strcmp(nearby_.buses[stop], bus_[b].id) != 0) ++stop;
        if (stop == nearby_.nBuses) continue;   // not one of ours (CTA only answers for the stops asked)
        Rec& r = s.buses[s.nBuses++];
        r.rec = bus_[b];
        r.rec.alert = isAlerted(r.rec.route);
        r.stop = static_cast<uint8_t>(stop);
    }
    s.loaded = everyStopPolled();
    s.located = located_;
    s.nClose = nearby_.nClose;
    memcpy(s.close, nearby_.close, sizeof(s.close));
    s.hasWeather = hasWeather_;
    s.wmo = wmo_;
    s.tempC = tempC_;
}

}  // namespace live
