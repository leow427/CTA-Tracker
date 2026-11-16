#include "live_store.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include "alerts_weather.h"
#include "bus_parse.h"
#include "text.h"
#include "train_cards.h"

namespace live {

bool Store::init() {
    stations_ = new (std::nothrow) Station[cta::kNearbyPerTab + 1]();   // the last is the pinned stop's own poll
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

void Store::setWeather(int wmo, float tempC) {
    hasWeather_ = true;
    wmo_ = wmo;
    tempC_ = tempC;
}

namespace {

uint32_t newest(const cta::StopRecord* r, int n, const char* id = nullptr) {
    uint32_t t = 0;
    for (int i = 0; i < n; ++i)
        if ((!id || strcmp(r[i].id, id) == 0) && r[i].ts > t) t = r[i].ts;
    return t;
}

}  // namespace

bool Store::applyRelay(const cta::RelayStop& s) {
    const bool pinned = hasPin_ && pin_.type == s.type && strcmp(pin_.id, s.id) == 0;
    const uint32_t ts = newest(s.records, s.n);
    if (s.type == cta::Kind::Train) {
        Station* st = nullptr;
        for (int i = 0; i < nearby_.nTrains && !st; ++i)
            if (strcmp(nearby_.trains[i], s.id) == 0) st = &stations_[i];
        if (!st && pinned) st = &stations_[cta::kNearbyPerTab];   // the pinned station's own poll
        if (!st) return false;
        if (s.n && ts < newest(st->cards, st->n)) return true;   // older than what's here (empty: nothing coming)
        const int n = s.n < kCardsPerStation ? s.n : kCardsPerStation;
        memcpy(st->cards, s.records, sizeof(cta::StopRecord) * n);
        st->n = n;
        st->ok = true;
        for (int c = 0; c < n; ++c) addRoute(st->cards[c].route);
        return true;
    }
    bool listed = pinned;
    for (int i = 0; i < nearby_.nBuses && !listed; ++i) listed = strcmp(nearby_.buses[i], s.id) == 0;
    if (!listed) return false;
    if (s.n && ts < newest(bus_, nBus_, s.id)) return true;
    int k = 0;   // the other stops' cards stay, in order; this stop's go to the end
    for (int b = 0; b < nBus_; ++b)
        if (strcmp(bus_[b].id, s.id) != 0) bus_[k++] = bus_[b];
    for (int r = 0; r < s.n && k < kMaxBusCards; ++r) {
        bus_[k++] = s.records[r];
        addRoute(s.records[r].route);
    }
    nBus_ = k;
    busOk_ = true;
    return true;
}

namespace {

bool sameStop(const cta::StopRecord& r, const cta::PickedStop& p) { return r.type == p.type && strcmp(r.id, p.id) == 0; }

// The card for the pin's line and direction (a card keeps 24 bytes of a longer direction label, as followView reads it).
bool pinCard(const cta::StopRecord& r, const cta::PickedStop& p) {
    char dir[cta::kNameLen + 1];
    cta::copyTruncated(dir, p.dir, cta::kNameLen);
    return sameStop(r, p) && strcmp(r.route, p.route) == 0 && strcmp(r.dir, dir) == 0;
}

}  // namespace

void Store::setPin(const cta::PickedStop* p) {
    if (!p) {
        hasPin_ = hasFollow_ = false;
        return;
    }
    if (hasFollow_ && !cta::samePick(*p, follow_.stop)) hasFollow_ = false;
    if (!hasPin_ || p->type != pin_.type || strcmp(p->id, pin_.id) != 0)   // the old stop's data (not on the stack: 4 KB)
        memset(&stations_[cta::kNearbyPerTab], 0, sizeof(Station));
    pin_ = *p;
    hasPin_ = true;
}

void Store::setFollow(const cta::FollowMsg* f) {
    if (!f) {
        hasFollow_ = false;
        return;
    }
    setPin(&f->stop);
    follow_ = *f;
    memo_ = cta::FollowMemo();
    view_ = {cta::FollowState::Waiting, -1};
    hasFollow_ = true;
}

int Store::applyPin(const char* body, size_t len, uint32_t now) {
    if (!hasPin_ || pin_.type != cta::Kind::Train) return -1;   // a bus pin rides in the bus poll (applyBuses)
    cta::StopRecord* cards = staging_->train;
    const int n = cta::parseTrainCards(body, len, now, pin_.id, cards, kCardsPerStation);
    if (n < 0) return -1;
    Station& s = stations_[cta::kNearbyPerTab];
    memcpy(s.cards, cards, sizeof(cta::StopRecord) * n);
    s.n = n;
    s.ok = true;
    for (int c = 0; c < n; ++c) addRoute(cards[c].route);
    return n;
}

int Store::pinRecords() const {
    cta::StopRecord* out = staging_->atPin;
    constexpr int kMax = sizeof(Staging::atPin) / sizeof(Staging::atPin[0]);
    int n = 0;
    auto add = [&](const cta::StopRecord& r) {
        if (!sameStop(r, pin_)) return;
        for (int i = 0; i < n; ++i)
            if (strcmp(out[i].route, r.route) == 0 && strcmp(out[i].dir, r.dir) == 0) {
                if (r.ts > out[i].ts) out[i] = r;
                return;
            }
        if (n < kMax) out[n++] = r;
    };
    const Station& own = stations_[cta::kNearbyPerTab];
    for (int c = 0; c < own.n; ++c) add(own.cards[c]);
    if (pin_.type == cta::Kind::Bus) {
        for (int b = 0; b < nBus_; ++b) add(bus_[b]);
    } else {
        for (int st = 0; st < nearby_.nTrains; ++st)
            for (int c = 0; c < stations_[st].n; ++c) add(stations_[st].cards[c]);
    }
    return n;
}

void Store::updateFollow(uint32_t now) {
    if (!hasFollow_) return;
    view_ = cta::followView(staging_->atPin, pinRecords(), follow_, memo_, now);
    if (cta::followExpired(memo_, now)) setPin(nullptr);   // the follow and the pin it made: Home goes back to the closest stops
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
            r.rec.alert = r.rec.alert || isAlerted(r.rec.route);   // a relayed record may carry its own
            r.stop = static_cast<uint8_t>(st);
        }
    for (int b = 0; b < nBus_ && s.nBuses < kMaxBusCards; ++b) {
        int stop = 0;
        while (stop < nearby_.nBuses && strcmp(nearby_.buses[stop], bus_[b].id) != 0) ++stop;
        if (stop == nearby_.nBuses) continue;   // not one of ours (CTA only answers for the stops asked)
        Rec& r = s.buses[s.nBuses++];
        r.rec = bus_[b];
        r.rec.alert = r.rec.alert || isAlerted(r.rec.route);
        r.stop = static_cast<uint8_t>(stop);
    }
    s.loaded = everyStopPolled();
    s.located = located_;
    s.nClose = nearby_.nClose;
    memcpy(s.close, nearby_.close, sizeof(nearby_.close));
    s.hasPin = hasPin_;
    if (hasPin_) {
        bool listed = false;
        for (int i = 0; i < s.nClose; ++i) listed = listed || strcmp(s.close[i], pin_.id) == 0;
        if (!listed) snprintf(s.close[s.nClose++], sizeof(s.close[0]), "%s", pin_.id);   // spec 5.6: it hypes too
        const int n = pinRecords();
        int found = -1;
        for (int i = 0; i < n && found < 0; ++i)
            if (pinCard(staging_->atPin[i], pin_)) found = i;
        cta::StopRecord& r = s.pin.rec;
        if (found >= 0) {
            r = staging_->atPin[found];
        } else {   // not polled yet, or nothing on its line now: the pin's own names, no arrivals
            memset(&r, 0, sizeof(r));
            r.type = pin_.type;
            snprintf(r.id, sizeof(r.id), "%s", pin_.id);
            snprintf(r.route, sizeof(r.route), "%s", pin_.route);
            cta::copyTruncated(r.dir, pin_.dir, cta::kNameLen);
            snprintf(r.name, sizeof(r.name), "%s", pin_.name);
        }
        r.alert = r.alert || isAlerted(r.route);
        s.pin.stop = 0;
    }
    s.hasFollow = hasFollow_;
    if (hasFollow_) s.follow = view_;
    s.hasWeather = hasWeather_;
    s.wmo = wmo_;
    s.tempC = tempC_;
}

}  // namespace live
