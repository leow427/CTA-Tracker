#pragma once
// The one-letter compass direction a card shows beside its name: 'N', 'S', 'E' or 'W', or 0 when it isn't known.

namespace cta {

// A bus route's direction from Bus Tracker's rtdir: "Southbound" → 'S'.
char busCompass(const char* rtdir);

// A train's direction from its line (GTFS route_id) and Train Tracker's destNm. The feed gives no compass point,
// so this is a fixed table of each line's ends, as a rider thinks of them: lines that run diagonally take their
// main heading (Blue to O'Hare is north, Orange to Midway south), and Loop-bound trains go the way they enter the
// Loop. An unknown line or destination (a rare short turn) gives 0, and the card shows no letter.
char trainCompass(const char* route, const char* dest);

}  // namespace cta
