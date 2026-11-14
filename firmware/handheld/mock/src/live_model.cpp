#include "live_model.h"

#include <stdio.h>
#include <string.h>

#include "compass.h"

#include "mood.h"

namespace mock {

namespace {

bool isClose(const live::Snapshot& s, const char* id) {
    for (int i = 0; i < s.nClose; ++i)
        if (strcmp(s.close[i], id) == 0) return true;
    return false;
}

// The card as of now: minutes counted down since the fetch, passed arrivals dropped. False if none are left. A stale
// record stays as it was fetched, greyed out on screen (spec 8.3): counting it down would empty the lists offline.
bool age(Card& c, uint32_t now) {
    cta::StopRecord& rec = c.rec;
    const bool stale = cta::isStale(rec, now);
    const int gone = !stale && rec.ts && now > rec.ts ? static_cast<int>((now - rec.ts) / 60) : 0;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < rec.count; ++i) {
        cta::Arrival a = rec.arrivals[i];
        a.min = static_cast<int16_t>(a.min - gone);
        if (a.min < 0) continue;
        rec.arrivals[kept++] = a;
    }
    rec.count = kept;
    c.sched = kept > 0 && !rec.arrivals[0].live;
    return kept > 0;
}

// The nearby message ranks stops without distances, so the rank stands in for one: nearest stop first.
void fill(Card& c, const live::Rec& r, const live::Snapshot& s) {
    memset(&c, 0, sizeof(c));
    c.rec = r.rec;
    c.station = r.stop;
    c.distKm = r.stop;
    c.close = isClose(s, c.rec.id);
}

// "Westbound" → "West".
void compass(const char* rtdir, char* out, size_t cap) {
    snprintf(out, cap, "%s", rtdir);
    const size_t n = strlen(out);
    if (n > 5 && strcmp(out + n - 5, "bound") == 0) out[n - 5] = 0;
}

// A train card's looks: the station's other lines as pips, the heading, the compass.
void trainLooks(Card& c, const live::Snapshot& s) {
    for (int j = 0; j < s.nTrains && c.nPips < 3; ++j) {
        const live::Rec& o = s.trains[j];
        if (strcmp(o.rec.id, c.rec.id) != 0 || strcmp(o.rec.route, c.rec.route) == 0) continue;
        bool seen = false;
        for (int k = 0; k < c.nPips; ++k) seen = seen || strcmp(c.pips[k], o.rec.route) == 0;
        if (!seen) c.pips[c.nPips++] = o.rec.route;   // points into s, which outlives this model update
    }
    snprintf(c.heading, sizeof(c.heading), "To %s", c.rec.count ? c.rec.arrivals[0].dest : c.rec.dir);
    c.compass = cta::trainCompass(c.rec.route, c.rec.count ? c.rec.arrivals[0].dest : c.rec.dir);
}

// A bus card's looks: "West to Central" and the compass.
void busLooks(Card& c) {
    char dir[16];
    compass(c.rec.dir, dir, sizeof(dir));
    snprintf(c.heading, sizeof(c.heading), "%s to %s", dir, c.rec.count ? c.rec.arrivals[0].dest : "?");
    c.compass = cta::busCompass(c.rec.dir);
}

}  // namespace

void fromLive(Model& m, const live::Snapshot& s, const power::State& p, uint32_t now, bool clockKnown) {
    m.now = now;
    m.live = true;
    m.clockKnown = clockKnown;
    m.fetchedAt = 0;
    bool anyFresh = false;

    m.nTrains = 0;
    for (int i = 0; i < s.nTrains && m.nTrains < kMaxCards; ++i) {
        const live::Rec& r = s.trains[i];
        Card& c = m.trains[m.nTrains];
        fill(c, r, s);
        if (!age(c, now)) continue;
        ++m.nTrains;
        trainLooks(c, s);
    }
    m.nBuses = 0;
    for (int i = 0; i < s.nBuses && m.nBuses < kMaxCards; ++i) {
        const live::Rec& r = s.buses[i];
        Card& c = m.buses[m.nBuses];
        fill(c, r, s);
        if (!age(c, now)) continue;
        ++m.nBuses;
        busLooks(c);
    }
    // One card per route and direction, at the nearest listed stop that still has an arrival (spec 8.3): a farther
    // stop may serve a route a nearer one shows. After the countdown, so a route whose nearer card just ran out of
    // arrivals falls back to the farther stop instead of vanishing until the next poll.
    int kept = 0;
    for (int i = 0; i < m.nBuses; ++i) {
        bool nearer = false;
        for (int j = 0; j < m.nBuses && !nearer; ++j)
            nearer = m.buses[j].station < m.buses[i].station && strcmp(m.buses[j].rec.route, m.buses[i].rec.route) == 0 &&
                     strcmp(m.buses[j].rec.dir, m.buses[i].rec.dir) == 0;
        if (!nearer) m.buses[kept++] = m.buses[i];
    }
    m.nBuses = kept;
    // Freshness from what was fetched, not what's left after the countdown.
    for (int i = 0; i < s.nTrains + s.nBuses; ++i) {
        const cta::StopRecord& r = i < s.nTrains ? s.trains[i].rec : s.buses[i - s.nTrains].rec;
        if (r.ts > m.fetchedAt) m.fetchedAt = r.ts;
        anyFresh = anyFresh || !cta::isStale(r, now);
    }

    // The pinned stop's Home card, built as the lists' cards are, and the follow on it (ecosystem spec 5.6, 5.7). A
    // pin with no arrivals left still shows. The follow's minutes count down with the card's (same record, same ts).
    m.hasPin = s.hasPin;
    if (s.hasPin) {
        fill(m.pin, s.pin, s);
        age(m.pin, now);
        if (m.pin.rec.type == cta::Kind::Bus) busLooks(m.pin);
        else trainLooks(m.pin, s);
    }
    m.hasFollow = s.hasFollow;
    m.follow = s.follow;
    const cta::StopRecord& pr = s.pin.rec;
    if (m.hasFollow && m.follow.state == cta::FollowState::Waiting && m.follow.min > 0 && pr.ts && now > pr.ts &&
        !cta::isStale(pr, now)) {
        const int left = m.follow.min - static_cast<int>((now - pr.ts) / 60);
        m.follow.min = static_cast<int16_t>(left > 0 ? left : 0);
    }

    m.loaded = s.loaded || m.nTrains + m.nBuses > 0;
    m.offline = s.status[0] != 0 || (s.nTrains + s.nBuses > 0 && !anyFresh);
    snprintf(m.status, sizeof(m.status), "%s", s.status);
    // Docked (spec 8.4): USB power and the home network. A board with no battery is on USB whenever it's on at all,
    // so it never counts as docked.
    m.powerDocked = p.usb && p.battery && s.homeWifi;
    m.battery = p.battery ? p.percent : -1;
    m.charging = p.charging;
    m.hasWeather = s.hasWeather;
    m.wmo = s.wmo;
    m.tempC = s.tempC;
    if (s.hasWeather && !m.weatherForced) m.weather = weatherFor(s.wmo);
    finish(m);
}

}  // namespace mock
