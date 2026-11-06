#include "line_colors.h"

#include <stddef.h>

namespace cta {

namespace {

struct LineColor {
    const char* route;
    uint32_t rgb;
};

constexpr LineColor kLineColors[] = {
    {"Red", 0xC60C30}, {"Blue", 0x00A1DE}, {"Brn", 0x62361B}, {"G", 0x009B3A},
    {"Org", 0xF9461C}, {"Pink", 0xE27EA6}, {"P", 0x522398},   {"Y", 0xF9E300},
};

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        if (lower(*a) != lower(*b)) return false;
    }
    return *a == *b;
}

}  // namespace

uint32_t lineColor(const char* route) {
    if (route == nullptr) return kBusColor;
    for (const LineColor& c : kLineColors) {
        if (equalsIgnoreCase(route, c.route)) return c.rgb;
    }
    return kBusColor;
}

}  // namespace cta
