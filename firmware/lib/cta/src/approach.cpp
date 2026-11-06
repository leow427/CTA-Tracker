#include "approach.h"

#include <string.h>

#include "json_util.h"
#include "text.h"

namespace cta {

bool buildTrainRow(const char* const* seq, size_t n, const char* stopId, ApproachRow& out) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(seq[i], stopId) != 0) continue;
        out = ApproachRow{};
        out.before = -1;
        out.pid = -1;
        for (int k = 6; k >= 0; k--) {
            long j = (long)i - (6 - k);
            if (j >= 0) copyTruncated(out.stops[k], seq[j], kIdLen);
        }
        return true;
    }
    return false;
}

const TrainLine* trainLineFor(const char* route, const char* stopId) {
    for (size_t l = 0; l < kTrainLineCount; l++) {
        const TrainLine& line = kTrainLines[l];
        if (!sameRoute(line.route, route)) continue;
        for (size_t i = line.lead; i < line.count; i++)
            if (strcmp(line.stopIds[i], stopId) == 0) return &line;
    }
    return nullptr;
}

size_t trainStations(const char* route, const char* dirLabel, StationRef* out, size_t max) {
    size_t n = 0;
    for (size_t l = 0; l < kTrainLineCount && n < max; l++) {
        const TrainLine& line = kTrainLines[l];
        if (strcmp(line.route, route) != 0 || strcmp(line.dirLabel, dirLabel) != 0) continue;
        for (size_t k = line.lead; k < line.count && n < max; k++) {
            bool dup = false;
            for (size_t j = 0; j < n && !dup; j++) dup = !strcmp(out[j].line->stopIds[out[j].index], line.stopIds[k]);
            if (!dup) out[n++] = StationRef{&line, (uint8_t)k};
        }
    }
    return n;
}

int buildBusRows(const char* json, size_t len, const char* stopId, ApproachRow* out, size_t max) {
    Doc doc;
    auto build = [](JsonDocument& f, bool many) {
        JsonVariant p = listItem(f["bustime-response"]["ptr"], many);
        p["pid"] = true;
        JsonObject pt = p["pt"][0].to<JsonObject>();   // a pattern always has many points
        for (const char* k : {"typ", "stpid", "pdist"}) pt[k] = true;
    };
    if (deserializeList(doc, json, len, build, [](JsonDocument& d) { return d["bustime-response"]["ptr"]; })) return -1;
    if (!doc["bustime-response"].is<JsonObjectConst>()) return -1;
    size_t n = 0;
    forEachItem(doc["bustime-response"]["ptr"], [&](JsonVariantConst p) {
        long pid;
        if (n >= max || !looseLong(p["pid"], pid)) return;
        // Ring of the last 7 stops seen; stop when the picked one arrives.
        const char* ids[8] = {};
        long dist[8] = {};
        size_t seen = 0;
        bool found = false;
        forEachItem(p["pt"], [&](JsonVariantConst point) {
            if (found || strcmp(point["typ"] | "", "S") != 0) return;
            const char* id = point["stpid"];
            long d;
            if (!id || !looseLong(point["pdist"], d)) return;
            ids[seen % 8] = id;
            dist[seen % 8] = d;
            seen++;
            if (strcmp(id, stopId) == 0) found = true;
        });
        if (!found) return;
        ApproachRow& row = out[n++];
        row = ApproachRow{};
        row.pid = (int32_t)pid;
        row.before = -1;
        for (int k = 6; k >= 0; k--) {
            long j = (long)seen - 1 - (6 - k);
            if (j < 0) continue;
            copyTruncated(row.stops[k], ids[j % 8], kIdLen);
            row.pdist[k] = (int32_t)dist[j % 8];
        }
        if (seen >= 8) row.before = (int32_t)dist[(seen - 8) % 8];
    });
    return (int)n;
}

uint8_t litMaskTrains(const ApproachRow& row, const char* route, const TrainPos* t, size_t n) {
    uint8_t mask = 0;
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 7 && sameRoute(t[i].route, route); k++)
            if (row.stops[k][0] && strcmp(row.stops[k], t[i].nextStpId) == 0) mask |= 1 << k;
    return mask;
}

uint8_t litMaskBuses(const ApproachRow* rows, size_t nRows, const BusVehicle* v, size_t n) {
    uint8_t mask = 0;
    for (size_t r = 0; r < nRows; r++) {
        const ApproachRow& row = rows[r];
        for (size_t i = 0; i < n; i++) {
            if (v[i].pid != row.pid) continue;
            long lower = row.before;
            for (int k = 0; k < 7; k++) {
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
