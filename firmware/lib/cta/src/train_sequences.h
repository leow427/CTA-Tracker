#pragma once
// Train line stop sequences, generated into train_sequences.cpp by tools/gtfs_build.py (plan 1, task 10).
#include <stddef.h>
#include <stdint.h>

namespace cta {

struct TrainLine {
    const char* route;              // GTFS route_id: Red, Blue, Brn, G, Org, P, Pink, Y
    const char* dirLabel;           // the direction: Train Tracker's destNm ("Howard", "Loop"), or on a trunk two
                                    // branches share, both joined ("Ashland/63rd & Cottage Grove")
    const char* const* stopIds;     // platform stop_ids, in travel order
    const char* const* names;       // station names, parallel to stopIds
    uint8_t count;                  // stops, lead-in included
    uint8_t lead;                   // the first `lead` stops come before this direction starts: they feed the
                                    // approach LEDs but aren't offered as picks
};

extern const TrainLine kTrainLines[];
extern const size_t kTrainLineCount;

}  // namespace cta
