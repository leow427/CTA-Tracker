#include "dock_status.h"

#include <stdio.h>
#include <string.h>

#include "cta_config.h"
#include "mood.h"

namespace cta {

int minutesLeft(const StopRecord& r, int k, uint32_t now) {
    const int gone = r.ts && now > r.ts ? static_cast<int>((now - r.ts) / 60) : 0;
    const int m = r.arrivals[k].min - gone;
    return m < 0 ? 0 : m;
}

bool dueNow(const StopRecord& r, uint32_t now) {
    if (!r.count || isStale(r, now)) return false;
    return r.arrivals[0].live && minutesLeft(r, 0, now) <= kHypedMaxMin;
}

bool cardStale(const StopRecord& r, uint32_t now, bool wifiLost) { return r.ts && (wifiLost || isStale(r, now)); }

Mood dockMood(const StopRecord* recs, size_t n, uint32_t now, bool night) {
    StopRecord counted[8];
    bool eligible[8];
    if (n > 8) n = 8;
    for (size_t i = 0; i < n; ++i) {
        counted[i] = recs[i];
        for (int k = 0; k < counted[i].count; ++k) counted[i].arrivals[k].min = static_cast<int16_t>(minutesLeft(recs[i], k, now));
        eligible[i] = true;   // on the dock all 3 stops qualify (spec 6.3)
    }
    return deviceMood(counted, eligible, n, now, night, true);
}

const char* trainLineName(const char* route) {
    static const char* const kNames[][2] = {{"Red", "Red"}, {"Blue", "Blue"}, {"Brn", "Brown"}, {"G", "Green"},
                                            {"Org", "Orange"}, {"Pink", "Pink"}, {"P", "Purple"}, {"Y", "Yellow"}};
    for (const auto& n : kNames)
        if (strcmp(n[0], route) == 0) return n[1];
    return route;
}

void moodReason(const StopRecord* recs, size_t n, uint32_t now, bool night, bool wifiLost, const char* status, char* out,
                size_t cap) {
    uint32_t newest = 0;
    for (size_t i = 0; i < n; ++i) if (recs[i].ts > newest) newest = recs[i].ts;
    const int age = newest && now > newest ? static_cast<int>((now - newest) / 60) : 0;
    if (wifiLost && newest) { snprintf(out, cap, "Data is %d min old", age); return; }
    if (night) { snprintf(out, cap, "Night hours"); return; }
    if (!newest) { snprintf(out, cap, "%s", status && status[0] ? status : "No data yet"); return; }
    const Mood m = dockMood(recs, n, now, night);
    for (size_t i = 0; i < n; ++i) {
        const StopRecord& r = recs[i];
        if (!r.ts || isStale(r, now)) continue;
        const bool bus = r.type == Kind::Bus;
        const char* label = trainLineName(r.route);
        const int min = r.count ? minutesLeft(r, 0, now) : 0;
        switch (m) {
            case Mood::Hyped:
                if (dueNow(r, now)) { snprintf(out, cap, min <= 1 ? "%s is due!" : "%s in %d min!", label, min); return; }
                break;
            case Mood::Sad:
                if (r.alert) { snprintf(out, cap, bus ? "%s alert" : "%s Line alert", label); return; }
                if (!r.count) { snprintf(out, cap, "No %s %s", label, bus ? "buses" : "trains"); return; }
                break;
            case Mood::Worried:
                if (r.count && r.arrivals[0].delayed) { snprintf(out, cap, "%s is running late", label); return; }
                break;
            default:
                if (r.count && r.arrivals[0].live) { snprintf(out, cap, min <= 1 ? "%s is due" : "%s in %d min", label, min); return; }
                break;
        }
    }
    snprintf(out, cap, "Data is %d min old", age);
}

}  // namespace cta
