#include "compass.h"

#include <ctype.h>
#include <string.h>

namespace cta {

namespace {

bool sameText(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

// dest starts with prefix, ignoring case: "95th/Dan Ryan" matches "95th".
bool startsWith(const char* dest, const char* prefix) {
    for (; *prefix; ++dest, ++prefix)
        if (!*dest || tolower(static_cast<unsigned char>(*dest)) != tolower(static_cast<unsigned char>(*prefix))) return false;
    return true;
}

struct End {
    const char* route;
    const char* dest;   // a prefix of destNm
    char compass;
};

const End kEnds[] = {
    {"Red", "Howard", 'N'},        {"Red", "95th", 'S'},          {"Red", "Roosevelt", 'S'},
    {"Blue", "O'Hare", 'N'},       {"Blue", "Rosemont", 'N'},     {"Blue", "Jefferson Park", 'N'},
    {"Blue", "Forest Park", 'W'},  {"Blue", "UIC", 'W'},
    {"Brn", "Kimball", 'N'},       {"Brn", "Loop", 'S'},
    {"G", "Harlem", 'W'},          {"G", "Ashland", 'S'},         {"G", "Cottage Grove", 'S'},
    {"Org", "Midway", 'S'},        {"Org", "Loop", 'N'},
    {"Pink", "54th", 'W'},         {"Pink", "Loop", 'E'},
    {"P", "Linden", 'N'},          {"P", "Howard", 'S'},          {"P", "Loop", 'S'},
    {"Y", "Skokie", 'N'},          {"Y", "Dempster", 'N'},        {"Y", "Howard", 'S'},
};

}  // namespace

char busCompass(const char* rtdir) {
    if (!rtdir) return 0;
    const char c = static_cast<char>(toupper(static_cast<unsigned char>(rtdir[0])));
    return c == 'N' || c == 'S' || c == 'E' || c == 'W' ? c : 0;
}

char trainCompass(const char* route, const char* dest) {
    if (!route || !dest) return 0;
    for (const End& e : kEnds)
        if (sameText(route, e.route) && startsWith(dest, e.dest)) return e.compass;
    return 0;
}

}  // namespace cta
