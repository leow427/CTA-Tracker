#include "mock_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cta_config.h"
#include "mood.h"
#ifdef POWER_LOG
#include "stress.h"
#endif

namespace mock {

namespace {

struct Station {
    const char* id;
    const char* name;
    const char* lines[3];
    uint8_t nLines;
    const char* line;   // the soonest at mock start, with dest and min
    const char* dest;
    int min;
    bool sched;
    double lat, lon;
};

// The mockup's ten nearest stations (docs/ideas/2026-11-04-handheld-ui-mockup.html).
const Station kStations[] = {
    {"41320", "Belmont", {"Red", "Brn", "P"}, 3, "Red", "95th/Dan Ryan", 2, false, 41.939751, -87.65338},
    {"41210", "Wellington", {"Brn", "P"}, 2, "Brn", "Loop", 3, false, 41.936033, -87.653266},
    {"41420", "Addison", {"Red"}, 1, "Red", "Howard", 4, false, 41.947316, -87.653624},
    {"40360", "Southport", {"Brn"}, 1, "Brn", "Kimball", 6, false, 41.943744, -87.663619},
    {"40530", "Diversey", {"Brn", "P"}, 2, "Brn", "Kimball", 5, false, 41.932732, -87.653131},
    {"40080", "Sheridan", {"Red"}, 1, "Red", "95th/Dan Ryan", 7, false, 41.953905, -87.654661},
    {"41220", "Fullerton", {"Red", "Brn", "P"}, 3, "Red", "Howard", 8, false, 41.9253, -87.652868},
    {"41310", "Paulina", {"Brn"}, 1, "Brn", "Loop", 9, false, 41.943623, -87.670907},
    {"41440", "Addison", {"Brn"}, 1, "Brn", "Kimball", 11, false, 41.947028, -87.674642},
    {"41460", "Irving Park", {"Brn"}, 1, "Brn", "Loop", 14, true, 41.954521, -87.674868},
};

struct Dirs { const char* line; const char* to[2]; int headway; };
const Dirs kDirs[] = {{"Red", {"Howard", "95th/Dan Ryan"}, 7}, {"Brn", {"Kimball", "Loop"}, 8}, {"P", {"Linden", "Loop"}, 12}};

struct BusStop {
    const char* rt;
    const char* name;
    const char* compass;
    const char* dest;
    int min;
    bool sched;
    double lat, lon;
    int headway;
};

const BusStop kBusStops[] = {
    {"77", "Belmont & Sheffield", "West", "Harlem", 3, false, 41.93975, -87.65445, 9},
    {"22", "Clark & Belmont", "South", "Harrison", 4, false, 41.940088, -87.650909, 10},
    {"77", "Belmont & Sheffield", "East", "Broadway", 6, false, 41.9399, -87.6543, 9},
    {"8", "Halsted & Belmont", "North", "Waveland", 2, false, 41.939907, -87.64917, 10},
    {"22", "Clark & Belmont", "North", "Howard", 9, false, 41.939867, -87.650495, 10},
    {"36", "Broadway & Belmont", "South", "Harrison", 7, false, 41.939817, -87.644466, 12},
    {"8", "Halsted & Belmont", "South", "79th", 12, false, 41.939744, -87.649357, 10},
    {"151", "Sheridan & Belmont", "South", "Union Station", 10, false, 41.9402, -87.6394, 10},
    {"152", "Addison & Sheffield", "West", "Harlem", 15, true, 41.9472, -87.6545, 15},
    {"9", "Ashland & Belmont", "North", "Irving Park", 18, false, 41.94003, -87.66855, 12},
};

const Dirs* dirsFor(const char* line) {
    for (const Dirs& d : kDirs)
        if (strcmp(d.line, line) == 0) return &d;
    return nullptr;
}

float distKm(double lat, double lon) {
    const double kx = 111.32 * cos(kMeLat * M_PI / 180), ky = 111.32;
    return static_cast<float>(hypot((lon - kMeLon) * kx, (lat - kMeLat) * ky));
}

void copy(char* dst, size_t cap, const char* src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

// Fills arrivals for a route running every `headway` minutes, the first `first` minutes after the mock started.
void simulate(cta::StopRecord& r, float elapsedMin, float first, int headway, bool sched, const char* dest) {
    float k0 = ceilf((elapsedMin - first) / headway);
    if (k0 < 0) k0 = 0;
    r.count = 0;
    for (int k = 0; k < static_cast<int>(cta::kMaxArrivals); ++k) {
        const float tta = first + (k0 + k) * headway - elapsedMin;
        cta::Arrival& a = r.arrivals[r.count++];
        a.min = static_cast<int16_t>(floorf(tta));
        copy(a.dest, sizeof(a.dest), dest);
        a.live = !sched;
        a.delayed = false;
    }
}

void delay(Card& c, int by) {
    c.delayMin = by;
    c.rec.arrivals[0].min += by;
    c.rec.arrivals[0].delayed = true;
}

// Nearest stop first, soonest first within it.
void sortCards(Card* cs, int n) {
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0; --j) {
            Card& a = cs[j - 1];
            Card& b = cs[j];
            const bool sameStop = a.station == b.station;
            // Different stops at the same distance keep their list order (station is nearest first).
            const bool swap = sameStop ? b.rec.arrivals[0].min < a.rec.arrivals[0].min
                                       : b.distKm < a.distKm - 1e-4f || (fabsf(b.distKm - a.distKm) <= 1e-4f && b.station < a.station);
            if (!swap) break;
            Card t = a; a = b; b = t;
        }
}

void say(Model& m, const Card* c, bool bus, const char* fmt, int value) {
    m.glance = c;
    m.glanceIsBus = bus;
    if (!c) return;
    char name[16];
    copy(name, sizeof(name), bus ? c->rec.route : lineName(c->rec.route));
    snprintf(m.reason, sizeof(m.reason), fmt, name, value);
}

}  // namespace

const char* scenarioName(Scenario s) {
    static const char* const kNames[] = {"Live", "Happy", "Hyped", "Worried", "Sad", "Sleepy", "Offline", "Docked",
                                         "First run", "Pairing"};
    return kNames[static_cast<int>(s)];
}

const char* lineName(const char* route) {
    static const char* const kMap[][2] = {{"Red", "Red"},  {"Blue", "Blue"}, {"Brn", "Brown"}, {"G", "Green"},
                                          {"Org", "Orange"}, {"Pink", "Pink"}, {"P", "Purple"}, {"Y", "Yellow"}};
    for (const auto& e : kMap)
        if (strcmp(e[0], route) == 0) return e[1];
    return route;
}

void weatherInfo(Weather w, const char*& icon, const char*& temp, uint32_t& rgb, int& wmo, float& tempC) {
    switch (w) {
        case Weather::Rain: icon = "cloud"; temp = "48\xC2\xB0"; rgb = 0x9db0d8; wmo = 61; tempC = 8.9f; return;
        case Weather::Snow: icon = "snow"; temp = "27\xC2\xB0"; rgb = 0xe8eef8; wmo = 73; tempC = -2.8f; return;
        default: icon = "sun"; temp = "41\xC2\xB0"; rgb = 0xffd95a; wmo = 0; tempC = 5.0f; return;
    }
}

namespace {

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = static_cast<int>(yoe) + static_cast<int>(era) * 400 + (m <= 2);
}

// The nth Sunday (1-based) of month m in year y, as days since the epoch.
int64_t nthSunday(int y, unsigned m, int n) {
    const int64_t first = daysFromCivil(y, m, 1);
    const int wd = static_cast<int>((first + 4) % 7);   // 1970-01-01 was a Thursday; 0 = Sunday
    return first + (7 - wd) % 7 + 7 * (n - 1);
}

}  // namespace

int localMinutes(uint32_t unix) {
    int y; unsigned mo, d;
    civilFromDays(static_cast<int64_t>(unix) / 86400, y, mo, d);
    // CDT from 2:00 CST on the second Sunday of March (08:00 UTC) to 2:00 CDT on the first Sunday of November (07:00 UTC).
    const int64_t start = nthSunday(y, 3, 2) * 86400 + 8 * 3600, end = nthSunday(y, 11, 1) * 86400 + 7 * 3600;
    const int offset = (unix >= start && unix < end) ? -5 * 3600 : -6 * 3600;
    const int64_t local = static_cast<int64_t>(unix) + offset;
    return static_cast<int>(((local % 86400) + 86400) % 86400 / 60);
}

void clockText(uint32_t unix, int offsetMin, char* out, size_t cap) {
    const int mins = localMinutes(unix + offsetMin * 60);
    int h = (mins / 60) % 12;
    if (h == 0) h = 12;
    snprintf(out, cap, "%d:%02d", h, mins % 60);
}

void update(Model& m, uint32_t now, uint32_t startUnix) {
    m.now = now;
    m.fetchedAt = now - (now - startUnix) % 30;   // a poll every 30 s
    const float elapsed = (now - startUnix) / 60.f;
    const Scenario sc = m.scenario;
    const bool offline = sc == Scenario::Offline;
    const uint32_t ts = offline ? now - 7 * 60 : m.fetchedAt;

    m.nTrains = 0;
    for (uint8_t s = 0; s < sizeof(kStations) / sizeof(kStations[0]); ++s) {
        const Station& st = kStations[s];
        for (uint8_t li = 0; li < st.nLines; ++li) {
            const Dirs* d = dirsFor(st.lines[li]);
            for (int di = 0; di < 2 && m.nTrains < kMaxCards; ++di) {
                Card& c = m.trains[m.nTrains++];
                memset(&c, 0, sizeof(c));
                const bool primary = strcmp(st.line, d->line) == 0 && strcmp(st.dest, d->to[di]) == 0;
                const float first = primary ? st.min : st.min + 2 + li * 3 + di * 4;
                c.sched = primary && st.sched;
                copy(c.rec.id, sizeof(c.rec.id), st.id);
                c.rec.type = cta::Kind::Train;
                copy(c.rec.route, sizeof(c.rec.route), d->line);
                copy(c.rec.dir, sizeof(c.rec.dir), d->to[di]);
                copy(c.rec.name, sizeof(c.rec.name), st.name);
                simulate(c.rec, elapsed, first, d->headway, c.sched, d->to[di]);
                c.rec.ts = ts;
                c.station = s;
                for (uint8_t k = 0; k < st.nLines; ++k)
                    if (k != li) c.pips[c.nPips++] = st.lines[k];
                c.lat = st.lat; c.lon = st.lon;
                c.distKm = distKm(st.lat, st.lon);
                snprintf(c.heading, sizeof(c.heading), "To %s", d->to[di]);
                // The scenario's nudges, as the mockup's flags() makes them.
                if (sc == Scenario::Worried && strcmp(d->line, "Brn") == 0) delay(c, 4);
                if (sc == Scenario::Sad && strcmp(d->line, "Red") == 0) c.rec.alert = true;
                if (sc == Scenario::Hyped && s == 0 && primary) c.rec.arrivals[0].min = 2;
                if (sc == Scenario::Happy && s == 0 && primary) c.rec.arrivals[0].min = 4;
            }
        }
    }
    m.nBuses = 0;
    for (uint8_t b = 0; b < sizeof(kBusStops) / sizeof(kBusStops[0]) && m.nBuses < kMaxCards; ++b) {
        const BusStop& bs = kBusStops[b];
        Card& c = m.buses[m.nBuses++];
        memset(&c, 0, sizeof(c));
        c.sched = bs.sched;
        snprintf(c.rec.id, sizeof(c.rec.id), "%d", 1000 + b);
        c.rec.type = cta::Kind::Bus;
        copy(c.rec.route, sizeof(c.rec.route), bs.rt);
        copy(c.rec.dir, sizeof(c.rec.dir), bs.dest);
        copy(c.rec.name, sizeof(c.rec.name), bs.name);
        simulate(c.rec, elapsed, bs.min, bs.headway, bs.sched, bs.dest);
        c.rec.ts = ts;
        c.station = b;
        c.lat = bs.lat; c.lon = bs.lon;
        c.distKm = distKm(bs.lat, bs.lon);
        snprintf(c.heading, sizeof(c.heading), "%s to %s", bs.compass, bs.dest);
        if (sc == Scenario::Worried && strcmp(bs.rt, "22") == 0) delay(c, 4);
    }
    // The 3 closest stops of either kind may hype (spec 10).
    float closest[3] = {1e9f, 1e9f, 1e9f};
    auto consider = [&](float d) {
        for (float& c : closest)
            if (fabsf(c - d) < 1e-4f) return;
        if (d < closest[2]) { closest[2] = d; if (closest[2] < closest[1]) { float t = closest[1]; closest[1] = closest[2]; closest[2] = t; }
                              if (closest[1] < closest[0]) { float t = closest[0]; closest[0] = closest[1]; closest[1] = t; } }
    };
    for (int i = 0; i < m.nTrains; ++i) consider(m.trains[i].distKm);
    for (int i = 0; i < m.nBuses; ++i) consider(m.buses[i].distKm);
    for (int i = 0; i < m.nTrains; ++i) m.trains[i].close = m.trains[i].distKm <= closest[2] + 1e-4f;
    for (int i = 0; i < m.nBuses; ++i) m.buses[i].close = m.buses[i].distKm <= closest[2] + 1e-4f;
    m.powerDocked = false;
    m.battery = 64;
    m.charging = true;
    finish(m);
}

Weather weatherFor(int wmo) {
    if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82) || (wmo >= 95 && wmo <= 99)) return Weather::Rain;
    if ((wmo >= 71 && wmo <= 77) || (wmo >= 85 && wmo <= 86)) return Weather::Snow;
    return Weather::Clear;
}

WeatherView weatherView(const Model& m) {
    WeatherView v{};
    const char* temp;
    weatherInfo(m.weather, v.icon, temp, v.rgb, v.wmo, v.tempC);
    snprintf(v.temp, sizeof(v.temp), "%s", temp);
    if (m.weatherForced) return v;
    if (!m.hasWeather) {
        if (m.live) { v.icon = nullptr; v.temp[0] = 0; v.wmo = 0; v.tempC = 10; }   // nothing yet; no accessory
        return v;
    }
    v.wmo = m.wmo;
    v.tempC = m.tempC;
    snprintf(v.temp, sizeof(v.temp), "%d\xC2\xB0", static_cast<int>(lroundf(m.tempC * 9 / 5 + 32)));
    if (m.weather == Weather::Clear && m.wmo >= 2) { v.icon = "cloud"; v.rgb = 0xc9d4ea; }   // cloudy or foggy
    return v;
}

void finish(Model& m) {
    const uint32_t now = m.now;
    const Scenario sc = m.scenario;
    sortCards(m.trains, m.nTrains);
    sortCards(m.buses, m.nBuses);

    // On the heap, so the device puts these 16 KB in PSRAM (internal RAM is for Wi-Fi and TLS).
    static cta::StopRecord* const recs = new cta::StopRecord[2 * kMaxCards];
    static bool eligible[2 * kMaxCards];
    static const Card* owners[2 * kMaxCards];
    int n = 0;
    for (int i = 0; i < m.nTrains; ++i) { recs[n] = m.trains[i].rec; owners[n] = &m.trains[i]; eligible[n++] = m.trains[i].close; }
    const int firstBus = n;
    for (int i = 0; i < m.nBuses; ++i) { recs[n] = m.buses[i].rec; owners[n] = &m.buses[i]; eligible[n++] = m.buses[i].close; }

    const int lm = localMinutes(now);
    bool night = lm >= cta::kNightStartMin && lm < cta::kNightEndMin;
#ifdef POWER_LOG
    if (stress::running()) night = false;   // stress-v2: every run measures daytime behaviour, whatever the hour
#endif
    const bool offline = sc == Scenario::Offline || m.offline;
    m.docked = sc == Scenario::Docked || (sc == Scenario::Live && m.powerDocked);
    switch (sc) {
        case Scenario::Live: m.mood = cta::deviceMood(recs, eligible, n, now, night, true); break;
        case Scenario::Happy: m.mood = cta::Mood::Happy; break;
        case Scenario::Hyped: m.mood = cta::Mood::Hyped; break;
        case Scenario::Worried: m.mood = cta::Mood::Worried; break;
        case Scenario::Sad: m.mood = cta::Mood::Sad; break;
        case Scenario::Pairing: m.mood = cta::Mood::Happy; break;
        default: m.mood = cta::Mood::Sleepy; break;
    }

    // Trackie's reason names the stop behind the mood, and that stop is the home screen's card.
    m.reason[0] = 0;
    m.glance = nullptr;
    // The home card: fresh before stale, then a close stop, then the soonest.
    const Card* soonest = nullptr;
    int soonestIdx = -1;
    static bool fresh[2 * kMaxCards];
    for (int i = 0; i < n; ++i) fresh[i] = !cta::isStale(recs[i], now);
    for (int i = 0; i < n; ++i) {
        if (!recs[i].count) continue;
        bool better = !soonest;
        if (!better && fresh[i] != fresh[soonestIdx]) better = fresh[i];
        else if (!better && eligible[i] != eligible[soonestIdx]) better = eligible[i];
        else if (!better) better = recs[i].arrivals[0].min < soonest->rec.arrivals[0].min;
        if (better) { soonest = owners[i]; soonestIdx = i; }
    }
    const bool soonestBus = soonestIdx >= firstBus;
    if (m.docked) {
        m.glance = soonest; m.glanceIsBus = soonestBus;
        if (m.battery < 0) snprintf(m.reason, sizeof(m.reason), "On USB power");
        else snprintf(m.reason, sizeof(m.reason), m.charging ? "Charging, %d%%" : "Charged, %d%%", m.battery);
        return;
    }
    if (offline) {
        m.glance = soonest; m.glanceIsBus = soonestBus;
        const uint32_t ts = m.live ? m.fetchedAt : soonest ? soonest->rec.ts : 0;
        if (m.status[0] && sc == Scenario::Live) snprintf(m.reason, sizeof(m.reason), "%s", m.status);
        else if (ts && now > ts) snprintf(m.reason, sizeof(m.reason), "Data is %d min old", static_cast<int>((now - ts) / 60));
        else snprintf(m.reason, sizeof(m.reason), "No data yet");
        return;
    }
    switch (m.mood) {
        case cta::Mood::Hyped:
            for (int i = 0; i < n; ++i)
                if (eligible[i] && fresh[i] && recs[i].count && recs[i].arrivals[0].live && recs[i].arrivals[0].min <= cta::kHypedMaxMin) {
                    say(m, owners[i], i >= firstBus, recs[i].arrivals[0].min <= 1 ? "%s is due!" : "%s in %d min!", recs[i].arrivals[0].min);
                    return;
                }
            break;
        case cta::Mood::Sad:
            for (int i = 0; i < n; ++i)
                if (recs[i].alert && fresh[i]) { say(m, owners[i], i >= firstBus, i >= firstBus ? "%s alert" : "%s Line alert", 0); return; }
            break;
        case cta::Mood::Worried:
            for (int i = 0; i < n; ++i)
                if (fresh[i] && recs[i].count && recs[i].arrivals[0].delayed) {
                    // CTA flags a delay without saying how long; only the mock knows the minutes.
                    if (owners[i]->delayMin > 0) say(m, owners[i], i >= firstBus, "%s is %d min late", owners[i]->delayMin);
                    else say(m, owners[i], i >= firstBus, "%s is running late", 0);
                    return;
                }
            break;
        case cta::Mood::Sleepy:
            m.glance = soonest; m.glanceIsBus = soonestBus;
            snprintf(m.reason, sizeof(m.reason), night ? "Night hours" : "Nothing nearby");
            return;
        default: break;
    }
    if (soonest) say(m, soonest, soonestBus, soonest->rec.arrivals[0].min <= 1 ? "%s is due" : "%s in %d min", soonest->rec.arrivals[0].min);
}

}  // namespace mock
