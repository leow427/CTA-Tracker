// Checks the live build's data path on a laptop: live::Store (the nearby list, what each response adds, that a
// failed one keeps the last good data, the pinned stop and the follow) and mock::fromLive (the countdown, stale data kept, alerts per route, offline, close stops,
// the home card, docked). Uses the recorded Belmont response and small synthetic Bus Tracker and alerts replies.
// Build and run (from firmware/handheld/mock):
//   cc -O2 -c ../../native/lib/cJSON/cJSON.c -o /tmp/cJSON.o
//   c++ -std=c++17 -Isrc -I../../lib/cta/src -I../../native/lib/cJSON tools/live_test.cpp src/live_store.cpp \
//       src/live_model.cpp src/mock_data.cpp ../../lib/cta/src/*.cpp /tmp/cJSON.o -o /tmp/live_test && /tmp/live_test
#include <stdio.h>
#include <string.h>

#include <string>

#include "live_model.h"
#include "live_store.h"
#include "nearby_msg.h"

namespace {

int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                \
        }                                                              \
    } while (0)

std::string slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("FAIL: missing %s\n", path); ++failures; return std::string(); }
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

std::string prd(const char* stpid, const char* stpnm, const char* rt, const char* rtdir, const char* des, const char* prdtm) {
    return std::string("{\"tmstmp\":\"20261109 09:11\",\"typ\":\"A\",\"stpnm\":\"") + stpnm + "\",\"stpid\":\"" + stpid +
           "\",\"rt\":\"" + rt + "\",\"rtdir\":\"" + rtdir + "\",\"des\":\"" + des + "\",\"prdtm\":\"" + prdtm +
           "\",\"dly\":false,\"prdctdn\":\"5\"}";
}

const mock::Card* find(const mock::Card* cs, int n, const char* id, const char* route) {
    for (int i = 0; i < n; ++i)
        if (strcmp(cs[i].rec.id, id) == 0 && strcmp(cs[i].rec.route, route) == 0) return &cs[i];
    return nullptr;
}

}  // namespace

int main() {
    // The Belmont fixture was fetched at 09:11:09 Chicago time on 2026-11-09 (CST): Unix 1794237069.
    const uint32_t t0 = 1794237069;
    static live::Store store;
    CHECK(store.init());
    CHECK(!store.located());
    const std::string nearbyJson = slurp("../../../fixtures/cta/nearby_belmont.json");
    cta::Nearby nb;
    CHECK(cta::parseNearby(nearbyJson.data(), nearbyJson.size(), nb));
    store.setNearby(nb);
    CHECK(store.located());

    const std::string belmont = slurp("../../../fixtures/cta/train_arrivals_belmont_live.json");
    CHECK(store.applyTrains(0, belmont.data(), belmont.size(), t0) == 6);
    CHECK(store.applyTrains(0, "{\"ctatt\":{\"errCd\":\"102\"}}", 25, t0 + 60) == -1);   // a bad reply...
    CHECK(store.applyTrains(99, belmont.data(), belmont.size(), t0) == -1);              // ...or station
    const std::string buses = std::string("{\"bustime-response\":{\"prd\":[") +
                              prd("14915", "Belmont & Clark", "77", "Westbound", "Central", "20261109 09:13") + "," +
                              prd("1921", "Clark & Belmont", "22", "Northbound", "Howard", "20261109 09:20") + "," +
                              prd("99999", "Not Ours", "36", "Southbound", "Harrison", "20261109 09:14") + "]}}";
    CHECK(store.applyBuses(buses.data(), buses.size(), t0) == 3);
    CHECK(store.applyBuses("{not json", 9, t0 + 60) == -1);

    const char* routes[live::Store::kMaxRoutes];
    const int nRoutes = store.routes(routes, live::Store::kMaxRoutes);
    CHECK(nRoutes == 6);   // Brn, Red, P, then 77, 22, 36 (harmless: it only widens the alerts call)
    const std::string alerts =
        "{\"CTAAlerts\":{\"ErrorCode\":\"0\",\"Alert\":{\"Headline\":\"Reroute\",\"ImpactedService\":{\"Service\":"
        "[{\"ServiceType\":\"B\",\"ServiceId\":\"22\"},{\"ServiceType\":\"R\",\"ServiceId\":\"P\"}]}}}}";
    CHECK(store.applyAlerts(alerts.data(), alerts.size()) == 2);

    static live::Snapshot snap;
    live::clear(snap);
    store.fill(snap);
    CHECK(snap.nTrains == 6);   // the failed second reply kept the first one's cards
    CHECK(snap.nBuses == 2);    // stop 99999 isn't in the preset
    CHECK(!snap.loaded);        // only one of the 5 stations polled
    CHECK(snap.located && snap.nClose == 3);

    power::State bench;   // USB, no battery
    bench.ok = bench.usb = true;
    static mock::Model m;

    // Fresh: everything shows, close stops may hype, the home card is a close stop.
    m = mock::Model();
    mock::fromLive(m, snap, bench, t0, true);
    CHECK(m.live && m.loaded && !m.offline && !m.docked);
    CHECK(m.nTrains == 6 && m.nBuses == 2);
    const mock::Card* b77 = find(m.buses, m.nBuses, "14915", "77");
    const mock::Card* b22 = find(m.buses, m.nBuses, "1921", "22");
    const mock::Card* red = find(m.trains, m.nTrains, "41320", "Red");
    CHECK(b77 && b22 && red);
    if (b77 && b22 && red) {
        CHECK(b77->close && b22->close && !red->close);
        CHECK(b22->rec.alert && !b77->rec.alert);
        CHECK(!red->rec.alert);
        const mock::Card* purple = find(m.trains, m.nTrains, "41320", "P");
        CHECK(purple && purple->rec.alert);
        CHECK(strcmp(b77->heading, "West to Central") == 0);
        CHECK(b77->rec.arrivals[0].min == 2);
        CHECK(red->nPips == 2);   // Brown and Purple also stop at Belmont
    }
    // The home card is a close stop (the 77, 2 min) over a sooner train (Brown, due) at a farther one.
    CHECK(m.glance && m.glanceIsBus && strcmp(m.glance->rec.route, "77") == 0);
    CHECK(m.mood == cta::Mood::Hyped);   // the 77 is 2 min away at a close stop

    // Buses gone stale while the trains are fresh: the home card and the reason come from fresh data.
    {
        static live::Snapshot mixed;
        mixed = snap;
        for (int i = 0; i < mixed.nBuses; ++i) mixed.buses[i].rec.ts = t0 - 400;
        m = mock::Model();
        mock::fromLive(m, mixed, bench, t0, true);
        CHECK(!m.offline);
        CHECK(m.glance && !m.glanceIsBus);
        CHECK(strstr(m.reason, "77") == nullptr);
    }

    // 2.5 min later: minutes count down, and the Brown toward Loop that was due has passed.
    m = mock::Model();
    mock::fromLive(m, snap, bench, t0 + 150, true);
    b77 = find(m.buses, m.nBuses, "14915", "77");
    CHECK(b77 && b77->rec.arrivals[0].min == 0);
    const mock::Card* brn = nullptr;
    for (int i = 0; i < m.nTrains; ++i)
        if (strcmp(m.trains[i].rec.route, "Brn") == 0 && strcmp(m.trains[i].rec.dir, "Loop") == 0) brn = &m.trains[i];
    CHECK(brn && brn->rec.count == 2 && brn->rec.arrivals[0].min == 4);   // 0 and 6 minus 2; 0 dropped
    CHECK(!m.offline);

    // 6 min later the data is stale: kept as fetched (greyed out), offline, and Trackie says how old it is.
    m = mock::Model();
    mock::fromLive(m, snap, bench, t0 + 360, true);
    CHECK(m.nTrains == 6 && m.nBuses == 2);
    b77 = find(m.buses, m.nBuses, "14915", "77");
    CHECK(b77 && b77->rec.arrivals[0].min == 2);
    CHECK(m.offline);
    CHECK(strcmp(m.reason, "Data is 6 min old") == 0);

    // The network side's status wins the reason.
    snprintf(snap.status, sizeof(snap.status), "Connecting to Wi-Fi");
    m = mock::Model();
    mock::fromLive(m, snap, bench, t0, false);
    CHECK(m.offline && !m.clockKnown);
    CHECK(strcmp(m.reason, "Connecting to Wi-Fi") == 0);
    CHECK(m.wifiSetUp && !m.bleConnected);   // Settings: a saved network, out of reach; no phone (the weak hook)
    snprintf(snap.status, sizeof(snap.status), "No Wi-Fi set up");
    mock::fromLive(m, snap, bench, t0, false);
    CHECK(!m.wifiSetUp);                     // Settings says "Not set up"
    snap.status[0] = 0;

    // Docked: USB, a battery, and home Wi-Fi; never without a battery.
    snap.homeWifi = true;
    m = mock::Model();
    mock::fromLive(m, snap, bench, t0, true);
    CHECK(!m.docked);
    power::State onDock = bench;
    onDock.battery = true;
    onDock.percent = 80;
    onDock.charging = true;
    m = mock::Model();
    mock::fromLive(m, snap, onDock, t0, true);
    CHECK(m.docked && strcmp(m.reason, "Charging, 80%") == 0);

    // No weather yet: nothing shown, no accessory.
    CHECK(mock::weatherView(m).icon == nullptr);

    // A route and direction shows once, at the nearest listed stop that still has an arrival: the 77 westbound at
    // 14915 (1st on the list) hides the one at 14913 (5th) until its only bus has passed, then 14913's shows.
    {
        const std::string twice = std::string("{\"bustime-response\":{\"prd\":[") +
                                  prd("14915", "Belmont & Clark", "77", "Westbound", "Central", "20261109 09:12") + "," +
                                  prd("14913", "Belmont & Clark", "77", "Westbound", "Central", "20261109 09:20") + "," +
                                  prd("14913", "Belmont & Clark", "77", "Eastbound", "Broadway", "20261109 09:15") + "]}}";
        CHECK(store.applyBuses(twice.data(), twice.size(), t0) == 3);
        static live::Snapshot d;
        live::clear(d);
        store.fill(d);
        CHECK(d.nBuses == 3);
        auto westAt = [&](uint32_t t) -> const char* {
            m = mock::Model();
            mock::fromLive(m, d, bench, t, true);
            const char* id = nullptr;
            int west = 0;
            for (int i = 0; i < m.nBuses; ++i)
                if (strcmp(m.buses[i].rec.dir, "Westbound") == 0) { id = m.buses[i].rec.id; ++west; }
            CHECK(west == 1 && m.nBuses == 2);
            return id ? id : "";
        };
        CHECK(strcmp(westAt(t0), "14915") == 0);
        CHECK(strcmp(westAt(t0 + 150), "14913") == 0);
    }

    // A new list drops the old stops' data until they're polled again.
    store.setNearby(nb);
    {
        static live::Snapshot e;
        live::clear(e);
        store.fill(e);
        CHECK(e.nTrains == 0 && e.nBuses == 0 && !e.loaded && e.located && !e.hasWeather);
    }

    // The pinned stop and the followed vehicle (ecosystem spec 5.6, 5.7).
    {
        static live::Store ps;
        static live::Snapshot s;
        CHECK(ps.init());
        cta::Nearby far;   // a list without Belmont
        const char* farJson = "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[\"41210\"],\"buses\":[\"1828\"],\"close\":[\"41210\",\"1828\"]}";
        CHECK(cta::parseNearby(farJson, strlen(farJson), far));
        ps.setNearby(far);
        auto stop = [](const char* route, const char* dir, const char* id, const char* name) {
            cta::PickedStop p = {};
            p.type = cta::Kind::Train;
            snprintf(p.route, sizeof p.route, "%s", route);
            snprintf(p.dir, sizeof p.dir, "%s", dir);
            snprintf(p.id, sizeof p.id, "%s", id);
            snprintf(p.name, sizeof p.name, "%s", name);
            return p;
        };
        auto look = [&]() { live::clear(s); ps.fill(s); };

        // Before any data: the pin's own names, no arrivals, and it counts as close.
        const cta::PickedStop red = stop("Red", "95th/Dan Ryan", "41320", "Belmont");
        ps.setPin(&red);
        look();
        CHECK(s.hasPin && !s.hasFollow);
        CHECK(strcmp(s.pin.rec.route, "Red") == 0 && strcmp(s.pin.rec.name, "Belmont") == 0 && s.pin.rec.count == 0 &&
              s.pin.rec.ts == 0);
        CHECK(s.nClose == 3 && strcmp(s.close[2], "41320") == 0);

        // Its own poll: the card for its line and direction, and no list card.
        CHECK(ps.applyPin(belmont.data(), belmont.size(), t0) == 6);
        look();
        CHECK(s.pin.rec.ts == t0 && strcmp(s.pin.rec.dir, "95th/Dan Ryan") == 0 && s.pin.rec.count >= 3);
        CHECK(strcmp(s.pin.rec.arrivals[0].vehicle, "911") == 0);
        CHECK(s.nTrains == 0);

        // Follow run 911 (8 min): waiting with its minutes; the same record again (a failed poll) changes nothing.
        cta::FollowMsg f = {};
        f.stop = red;
        snprintf(f.vehicle, sizeof f.vehicle, "911");
        snprintf(f.dest, sizeof f.dest, "95th/Dan Ryan");
        ps.setFollow(&f);
        ps.updateFollow(t0 + 5);
        look();
        CHECK(s.hasFollow && s.follow.state == cta::FollowState::Waiting && s.follow.min == 8);
        {   // the model's Home card: built as the list cards are, and the follow counted down with it
            static mock::Model pm;
            pm = mock::Model();
            mock::fromLive(pm, s, bench, t0 + 5, true);
            CHECK(pm.hasPin && pm.hasFollow && pm.follow.state == cta::FollowState::Waiting && pm.follow.min == 8);
            CHECK(strcmp(pm.pin.rec.route, "Red") == 0 && strcmp(pm.pin.heading, "To 95th/Dan Ryan") == 0);
            CHECK(pm.pin.nPips == 0);   // Belmont isn't in this list, so no other lines to show
            pm = mock::Model();
            mock::fromLive(pm, s, bench, t0 + 125, true);
            CHECK(pm.follow.min == 6 && pm.pin.rec.arrivals[0].min == 6);
            live::Snapshot none = s;
            none.hasPin = none.hasFollow = false;
            mock::fromLive(pm, none, bench, t0 + 125, true);
            CHECK(!pm.hasPin && !pm.hasFollow);
        }
        ps.updateFollow(t0 + 120);
        look();
        CHECK(s.hasFollow && s.follow.state == cta::FollowState::Waiting);

        // A list holding Belmont too: the newer record of the two is the pinned card; the pin and follow stay.
        const char* withJson = "{\"lat\":41.9,\"lon\":-87.6,\"trains\":[\"41320\"],\"buses\":[],\"close\":[\"41320\"]}";
        cta::Nearby with;
        CHECK(cta::parseNearby(withJson, strlen(withJson), with));
        ps.setNearby(with);
        CHECK(ps.applyTrains(0, belmont.data(), belmont.size(), t0 + 60) == 6);
        look();
        CHECK(s.pin.rec.ts == t0 + 60 && s.nClose == 1 && s.hasFollow);

        // A pin at the same stop keeps the follow; one at another stop ends it; clearing the pin clears both.
        ps.setPin(&red);
        look();
        CHECK(s.hasFollow);
        const cta::PickedStop brn = stop("Brn", "Loop", "41210", "Wellington");
        ps.setPin(&brn);
        look();
        CHECK(s.hasPin && !s.hasFollow && strcmp(s.pin.rec.id, "41210") == 0);
        ps.setFollow(&f);   // re-pins Belmont
        CHECK(ps.pin() && strcmp(ps.pin()->id, "41320") == 0);
        ps.setFollow(nullptr);
        CHECK(ps.pin() != nullptr);
        ps.setPin(nullptr);
        look();
        CHECK(!s.hasPin && !s.hasFollow);

        // Every follow ends: a seen vehicle past its minutes plus the grace is Arrived, then expires, leaving the pin.
        ps.setFollow(&f);
        ps.updateFollow(t0 + 70);   // sees 911 on the t0 + 60 record
        const uint32_t over = t0 + 60 + (8 + cta::kFollowGraceMin) * 60 + 1;
        ps.updateFollow(over);
        look();
        CHECK(s.hasFollow && s.follow.state == cta::FollowState::Arrived);
        ps.updateFollow(over + cta::kFollowEndedSecs);
        look();
        CHECK(!s.hasFollow && s.hasPin);

        // A pinned stop outside the list counts as close for Hyped: Red to Howard, live, due in about 1 min.
        ps.setNearby(far);
        const cta::PickedStop howard = stop("Red", "Howard", "41320", "Belmont");
        ps.setPin(&howard);
        CHECK(ps.applyPin(belmont.data(), belmont.size(), t0) == 6);
        look();
        CHECK(s.nTrains == 0 && s.pin.rec.count && s.pin.rec.arrivals[0].live && s.pin.rec.arrivals[0].min <= cta::kHypedMaxMin);
        {
            static mock::Model pm;
            pm = mock::Model();
            mock::fromLive(pm, s, bench, t0, true);
            CHECK(pm.hasPin && pm.mood == cta::Mood::Hyped);
            // The app's night hours (config.settings): a span past midnight holding this minute is night...
            const int lm = mock::localMinutes(t0);
            pm = mock::Model();
            pm.nightStart = static_cast<uint16_t>(lm);
            pm.nightEnd = static_cast<uint16_t>((lm + 1439) % 1440);
            mock::fromLive(pm, s, bench, t0, true);
            CHECK(pm.mood == cta::Mood::Sleepy && strcmp(pm.reason, "Night hours") == 0);
            // ...and one that doesn't hold it isn't.
            pm = mock::Model();
            pm.nightStart = static_cast<uint16_t>((lm + 1) % 1440);
            pm.nightEnd = static_cast<uint16_t>((lm + 2) % 1440);
            mock::fromLive(pm, s, bench, t0, true);
            CHECK(pm.mood != cta::Mood::Sleepy);
        }
        {   // A pinned stop with nothing left to come is no vote: a bus due in 12 min, and a pinned bus stop outside the
            // list whose only arrival has passed, isn't Sad.
            static live::Snapshot q;
            live::clear(q);
            q.located = q.loaded = true;
            auto bus = [](cta::StopRecord& r, const char* id, const char* route, int16_t min, uint32_t ts) {
                r.type = cta::Kind::Bus;
                snprintf(r.id, sizeof r.id, "%s", id);
                snprintf(r.route, sizeof r.route, "%s", route);
                snprintf(r.dir, sizeof r.dir, "Southbound");
                snprintf(r.name, sizeof r.name, "Stop %s", id);
                r.count = 1;
                r.arrivals[0].min = min;
                snprintf(r.arrivals[0].dest, sizeof r.arrivals[0].dest, "Harrison");
                r.arrivals[0].live = true;
                r.ts = ts;
            };
            bus(q.buses[0].rec, "1828", "22", 12, t0);
            q.nBuses = 1;
            q.hasPin = true;
            bus(q.pin.rec, "99999", "36", 1, t0 - 180);   // due a minute after it was fetched, 3 min ago
            static mock::Model pm;
            pm = mock::Model();
            mock::fromLive(pm, q, bench, t0, true);
            CHECK(pm.hasPin && pm.pin.rec.count == 0 && pm.mood != cta::Mood::Sad);
        }

        // A cleared pin drops its stop's data: pinned again, it has no record until its next poll.
        ps.setPin(nullptr);
        ps.setPin(&howard);
        look();
        CHECK(s.hasPin && s.pin.rec.ts == 0);

        // A bus pin outside the list rides in the bus poll: no poll of its own, its card from the batched reply.
        cta::PickedStop b36 = stop("36", "Southbound", "99999", "Not Ours");
        b36.type = cta::Kind::Bus;
        ps.setPin(&b36);
        CHECK(ps.applyPin(buses.data(), buses.size(), t0) == -1);
        CHECK(ps.applyBuses(buses.data(), buses.size(), t0) == 3);
        look();
        CHECK(s.pin.rec.type == cta::Kind::Bus && s.pin.rec.count == 1 && s.pin.rec.ts == t0 && s.nBuses == 0);
    }

    printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
