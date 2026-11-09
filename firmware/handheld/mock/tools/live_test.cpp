// Checks the live build's data path on a laptop: live::Store (the nearby list, what each response adds, and that a
// failed one keeps the last good data) and mock::fromLive (the countdown, stale data kept, alerts per route, offline, close stops,
// the home card, docked). Uses the recorded Belmont response and small synthetic Bus Tracker and alerts replies.
// Build and run (from firmware/handheld/mock):
//   c++ -std=c++17 -Isrc -I../../lib/cta/src -I<ArduinoJson>/src tools/live_test.cpp src/live_store.cpp \
//       src/live_model.cpp src/mock_data.cpp ../../lib/cta/src/*.cpp -o /tmp/live_test && /tmp/live_test
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

    printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
