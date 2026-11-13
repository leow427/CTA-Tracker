#include "approach.h"

#include <math.h>
#include <string.h>

#include "json_util.h"
#include "text.h"
#include "train_map.h"

namespace cta {

namespace {

constexpr size_t kIdMax = sizeof(ApproachRow::stops[0]) - 1;

bool sameRoute(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        const char x = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        const char y = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

ApproachRow emptyRow() {
    ApproachRow r;
    memset(&r, 0, sizeof(r));
    r.before = -1;
    r.pid = -1;
    return r;
}

// pdist and coordinates arrive as numbers (decimals) or numeric strings.
bool number(JsonVariantConst v, double& out) {
    if (v.is<double>() || v.is<long>()) { out = v.as<double>(); return true; }
    const char* s = v.as<const char*>();
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    out = strtod(s, &end);
    return *end == '\0' && isfinite(out);
}

// Parses getpatterns keeping each pattern's pid and its points' typ, stpid, pdist, lat and lon. ptr may be a list or
// one pattern, and a pattern's pt a list or one point.
bool parsePatterns(const char* json, size_t len, JsonDocument& doc) {
    JsonDocument asList, asObject;
    auto fill = [](JsonObject pattern) {
        pattern["pid"] = true;
        JsonObject pt = pattern["pt"][0].to<JsonObject>();
        for (const char* k : {"typ", "stpid", "pdist", "lat", "lon"}) pt[k] = true;
    };
    fill(asList["bustime-response"]["ptr"][0].to<JsonObject>());
    fill(asObject["bustime-response"]["ptr"].to<JsonObject>());
    if (!json::parseShapes(json, len, doc, {&asList, &asObject}, [](JsonDocument& d) {
            JsonVariantConst p = d["bustime-response"]["ptr"];
            return p.is<JsonArrayConst>() || p.is<JsonObjectConst>();
        }))
        return false;
    return doc["bustime-response"].is<JsonObjectConst>();
}

}  // namespace

bool buildTrainRow(const char* const* seq, size_t n, const char* stopId, ApproachRow& out) {
    if (seq == nullptr || stopId == nullptr) return false;
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(seq[i], stopId) != 0) continue;
        out = emptyRow();
        for (int k = 6; k >= 0; --k) {
            const long j = static_cast<long>(i) - (6 - k);
            if (j < 0) continue;
            copyTruncated(out.stops[k], seq[j], kIdMax);
            trainPlatformLatLon(out.stops[k], out.lat[k], out.lon[k]);
        }
        return true;
    }
    return false;
}

const TrainLine* trainLineFor(const char* route, const char* stopId) {
    if (route == nullptr || stopId == nullptr) return nullptr;
    for (size_t l = 0; l < kTrainLineCount; ++l) {
        const TrainLine& line = kTrainLines[l];
        if (!sameRoute(line.route, route)) continue;
        for (size_t i = line.lead; i < line.count; ++i)
            if (strcmp(line.stopIds[i], stopId) == 0) return &line;
    }
    return nullptr;
}

namespace {

// Short sequences that continue another: Green's two southern branches leave the trunk after Garfield, and the
// Loop-bound Purple Express runs on from Linden-Howard. GTFS gives each its own most-common pattern, so the stops
// before a branch's first one come from here (the feeder's last stops, in order).
struct Feeder { const char* route; const char* dirLabel; const char* fromDirLabel; };
constexpr Feeder kFeeders[] = {
    {"G", "Cottage Grove", "Ashland/63rd & Cottage Grove"},
    {"G", "Ashland/63rd", "Ashland/63rd & Cottage Grove"},
    {"P", "Loop", "Howard"},
};

const TrainLine* feederOf(const TrainLine& line) {
    for (const Feeder& f : kFeeders) {
        if (strcmp(f.route, line.route) != 0 || strcmp(f.dirLabel, line.dirLabel) != 0) continue;
        for (size_t l = 0; l < kTrainLineCount; ++l)
            if (strcmp(kTrainLines[l].route, f.route) == 0 && strcmp(kTrainLines[l].dirLabel, f.fromDirLabel) == 0)
                return &kTrainLines[l];
    }
    return nullptr;
}

}  // namespace

bool trainRowFor(const char* route, const char* stopId, ApproachRow& out) {
    const TrainLine* line = trainLineFor(route, stopId);
    if (!line) return false;
    const TrainLine* feeder = feederOf(*line);
    if (!feeder) return buildTrainRow(line->stopIds, line->count, stopId, out);
    // The feeder's last 6 stops, then the line's own.
    const char* seq[6 + 256];
    size_t n = 0;
    for (size_t i = feeder->count > 6 ? feeder->count - 6 : 0; i < feeder->count; ++i) seq[n++] = feeder->stopIds[i];
    for (size_t i = 0; i < line->count && n < sizeof(seq) / sizeof(seq[0]); ++i) seq[n++] = line->stopIds[i];
    return buildTrainRow(seq, n, stopId, out);
}

size_t trainStations(const char* route, const char* dirLabel, StationRef* out, size_t max) {
    size_t n = 0;
    for (size_t l = 0; l < kTrainLineCount && n < max; ++l) {
        const TrainLine& line = kTrainLines[l];
        if (strcmp(line.route, route) != 0 || strcmp(line.dirLabel, dirLabel) != 0) continue;
        for (size_t k = line.lead; k < line.count && n < max; ++k) {
            bool dup = false;
            for (size_t j = 0; j < n && !dup; ++j) dup = strcmp(out[j].line->stopIds[out[j].index], line.stopIds[k]) == 0;
            if (!dup) out[n++] = StationRef{&line, static_cast<uint8_t>(k)};
        }
    }
    return n;
}

size_t trainDirections(const char* route, const char** out, size_t max) {
    size_t n = 0;
    for (size_t l = 0; l < kTrainLineCount && n < max; ++l) {
        const TrainLine& line = kTrainLines[l];
        if (strcmp(line.route, route) != 0) continue;
        bool dup = false;
        for (size_t j = 0; j < n && !dup; ++j) dup = strcmp(out[j], line.dirLabel) == 0;
        if (!dup) out[n++] = line.dirLabel;
    }
    return n;
}

// No patterns in the reply: 0 or −1 by its errors (json::noListResult).
static bool noPatterns(JsonDocument& doc, int& result) {
    JsonVariantConst p = doc["bustime-response"]["ptr"];
    if (p.is<JsonArrayConst>() || p.is<JsonObjectConst>()) return false;
    result = json::noListResult(doc["bustime-response"]["error"]);
    return true;
}

int buildBusRows(const char* json, size_t len, const char* stopId, ApproachRow* out, size_t max) {
    if (stopId == nullptr) return -1;
    JsonDocument doc(json::bigAllocator());
    if (!parsePatterns(json, len, doc)) return -1;
    int none;
    if (noPatterns(doc, none)) return none;
    size_t n = 0;
    json::forEachItem(doc["bustime-response"]["ptr"], [&](JsonObjectConst p) {
        int32_t pid;
        if (n >= max || !json::toInt32(p["pid"], pid)) return;
        // A ring of the last 8 stops seen, stopping at the picked one.
        struct Seen { const char* id; double d, lat, lon; } ring[8] = {};
        size_t seen = 0;
        bool found = false;
        json::forEachItem(p["pt"], [&](JsonObjectConst pt) {
            if (found || strcmp(pt["typ"] | "", "S") != 0) return;
            const char* id = pt["stpid"];
            Seen s{id, 0, 0, 0};
            if (id == nullptr || !number(pt["pdist"], s.d)) return;
            if (!number(pt["lat"], s.lat) || !number(pt["lon"], s.lon)) s.lat = s.lon = 0;
            ring[seen % 8] = s;
            ++seen;
            if (strcmp(id, stopId) == 0) found = true;
        });
        if (!found) return;
        ApproachRow& row = out[n++];
        row = emptyRow();
        row.pid = pid;
        for (int k = 6; k >= 0; --k) {
            const long j = static_cast<long>(seen) - 1 - (6 - k);
            if (j < 0) continue;
            const Seen& s = ring[j % 8];
            copyTruncated(row.stops[k], s.id, kIdMax);
            row.pdist[k] = static_cast<int32_t>(lround(s.d));
            row.lat[k] = static_cast<float>(s.lat);
            row.lon[k] = static_cast<float>(s.lon);
        }
        if (seen >= 8) row.before = static_cast<int32_t>(lround(ring[(seen - 8) % 8].d));
    });
    return static_cast<int>(n);
}

int busPatternPath(const char* json, size_t len, int32_t pid, LatLon* out, size_t max) {
    JsonDocument doc(json::bigAllocator());
    if (!parsePatterns(json, len, doc)) return -1;
    int none;
    if (noPatterns(doc, none)) return none;
    int written = 0;
    json::forEachItem(doc["bustime-response"]["ptr"], [&](JsonObjectConst p) {
        int32_t id;
        if (written || !json::toInt32(p["pid"], id) || id != pid || max == 0) return;
        size_t total = 0;
        json::forEachItem(p["pt"], [&](JsonObjectConst pt) {
            double la, lo;
            if (number(pt["lat"], la) && number(pt["lon"], lo)) ++total;
        });
        // Keep point i when it starts a new step of total/max (evenly spaced), and always the last.
        size_t i = 0, n = 0;
        json::forEachItem(p["pt"], [&](JsonObjectConst pt) {
            double la, lo;
            if (!number(pt["lat"], la) || !number(pt["lon"], lo)) return;
            const bool keep = total <= max || i == total - 1 ||
                              (max > 1 && i * (max - 1) / (total - 1) != (i == 0 ? SIZE_MAX : (i - 1) * (max - 1) / (total - 1)));
            if (keep && n < max) out[n++] = LatLon{static_cast<float>(la), static_cast<float>(lo)};
            ++i;
        });
        written = static_cast<int>(n);
    });
    return written;
}

uint8_t litMaskTrains(const ApproachRow& row, const TrainPos* t, size_t n) {
    uint8_t mask = 0;
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < 7; ++k)
            if (row.stops[k][0] && strcmp(row.stops[k], t[i].nextStpId) == 0) mask |= 1 << k;
    return mask;
}

uint8_t litMaskBuses(const ApproachRow* rows, size_t nRows, const BusVehicle* v, size_t n) {
    uint8_t mask = 0;
    for (size_t r = 0; r < nRows; ++r) {
        const ApproachRow& row = rows[r];
        for (size_t i = 0; i < n; ++i) {
            if (v[i].pid != row.pid) continue;
            int32_t lower = row.before;
            for (int k = 0; k < 7; ++k) {
                if (!row.stops[k][0]) continue;
                if ((lower < 0 || v[i].pdist > lower) && v[i].pdist <= row.pdist[k]) {
                    mask |= 1 << k;
                    break;
                }
                lower = row.pdist[k];
            }
        }
    }
    return mask;
}

}  // namespace cta
