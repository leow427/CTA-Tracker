#include "line_colors.h"

#include <stddef.h>

namespace cta {

namespace {

struct LineColor {
    const char* route;
    uint32_t rgb;
};

constexpr LineColor kLineColors[] = {
    {"Red", 0xC60C30}, {"Blue", 0x2F7BFF}, {"Brn", 0x62361B}, {"G", 0x009B3A},
    {"Org", 0xF9461C}, {"Pink", 0xE27EA6}, {"P", 0x522398},   {"Y", 0xF9E300},
};

// Brown is orange at half brightness: true brown can't be lit.
constexpr LineColor kLedLineColors[] = {
    {"Red", 0xFF0000}, {"Blue", 0x0000FF}, {"Brn", 0x801000}, {"G", 0x00FF00},
    {"Org", 0xFF5000}, {"Pink", 0xFF0050}, {"P", 0x6000FF},   {"Y", 0xFFB000},
};

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        if (lower(*a) != lower(*b)) return false;
    }
    return *a == *b;
}

uint32_t find(const LineColor* t, size_t n, const char* route, uint32_t bus) {
    if (route == nullptr) return bus;
    for (size_t i = 0; i < n; ++i) {
        if (equalsIgnoreCase(route, t[i].route)) return t[i].rgb;
    }
    return bus;
}

}  // namespace

uint32_t lineColor(const char* route) { return find(kLineColors, 8, route, kBusColor); }
uint32_t ledLineColor(const char* route) { return find(kLedLineColors, 8, route, kLedBusColor); }

}  // namespace cta
