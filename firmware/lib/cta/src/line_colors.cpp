#include "line_colors.h"

#include <math.h>
#include <strings.h>

#include <initializer_list>

namespace cta {

const TrainLineInfo kTrainLineInfo[8] = {
    {"Red", "Red Line", 0xC60C30},    {"Blue", "Blue Line", 0x00A1DE},  {"Brn", "Brown Line", 0x62361B},
    {"G", "Green Line", 0x009B3A},    {"Org", "Orange Line", 0xF9461C}, {"Pink", "Pink Line", 0xE27EA6},
    {"P", "Purple Line", 0x522398},   {"Y", "Yellow Line", 0xF9E300},
};

const TrainLineInfo* trainLine(const char* route) {
    if (!route) return nullptr;
    for (const auto& l : kTrainLineInfo)
        if (strcasecmp(l.route, route) == 0) return &l;
    return nullptr;
}

uint32_t lineColor(const char* route) {
    const TrainLineInfo* l = trainLine(route);
    return l ? l->rgb : kBusColor;
}

uint32_t accent(const char* route) {
    const TrainLineInfo* l = trainLine(route);
    return l ? l->rgb : kBusAccent;
}

double luminance(uint32_t rgb) {
    auto channel = [&](int shift) {
        double c = ((rgb >> shift) & 0xFF) / 255.0;
        return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(16) + 0.7152 * channel(8) + 0.0722 * channel(0);
}

double contrast(uint32_t a, uint32_t b) {
    double la = luminance(a), lb = luminance(b);
    return (fmax(la, lb) + 0.05) / (fmin(la, lb) + 0.05);
}

// `rgb` scaled toward black to about `target` luminance, by at most `cap` of itself.
static uint32_t shaded(uint32_t rgb, double target, double cap) {
    double factor = fmin(cap, pow(target / luminance(rgb), 1 / 2.2));
    uint32_t out = 0;
    for (int shift : {16, 8, 0}) out |= (uint32_t)lround(((rgb >> shift) & 0xFF) * factor) << shift;
    return out;
}

// `rgb` mixed toward white in 10% steps, from `from` steps, until it reaches `minimum` on `bg`.
static uint32_t lifted(uint32_t rgb, uint32_t bg, double minimum, int from = 0) {
    for (int step = from; step <= 10; step++) {
        uint32_t out = 0;
        for (int shift : {16, 8, 0}) {
            double v = (rgb >> shift) & 0xFF;
            out |= (uint32_t)lround(v + (255 - v) * step / 10.0) << shift;
        }
        if (contrast(out, bg) >= minimum) return out;
    }
    return 0xFFFFFF;
}

uint32_t card(uint32_t rgb) { return shaded(rgb, 0.04, 0.85); }
uint32_t cardTop(uint32_t rgb) { return shaded(rgb, 0.09, 0.95); }
uint32_t cardText(uint32_t rgb) { return lifted(rgb, cardTop(rgb), 4.5, 3); }
uint32_t onNavy(uint32_t rgb, double minimum) { return lifted(rgb, kNavy, minimum); }

}  // namespace cta
