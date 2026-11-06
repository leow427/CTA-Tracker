#pragma once
// CTA line colors (base spec 6.5) and the app's night-navy look, mirrored from CTAKit LineColors
// so the devices draw the same cards as the iPhone (iOS redesign spec, revision 2).
#include <stdint.h>

namespace cta {

struct TrainLineInfo { const char* route; const char* name; uint32_t rgb; };
extern const TrainLineInfo kTrainLineInfo[8];

constexpr uint32_t kBusColor = 0x565A5C;    // LEDs and badges
constexpr uint32_t kBusAccent = 0x2F7BFF;   // bus cards on navy
constexpr uint32_t kNavy = 0x0B1A36;        // panels
constexpr uint32_t kRaised = 0x17294F;      // chips, buttons
constexpr uint32_t kMist = 0x9DB0D8;        // secondary text on navy
constexpr uint32_t kHeadlight = 0xFFD95A;   // signature yellow
constexpr uint32_t kCream = 0xF4ECC8;       // the pet's tile
constexpr uint32_t kAmber = 0xFFB000;       // Delayed
constexpr uint32_t kSignal = 0x34C759;      // live glyph

const TrainLineInfo* trainLine(const char* route);   // case-insensitive; nullptr for buses
uint32_t lineColor(const char* route);                // the line's CTA color, kBusColor for every bus
uint32_t accent(const char* route);                   // the line's color, kBusAccent for buses

double luminance(uint32_t rgb);
double contrast(uint32_t a, uint32_t b);
uint32_t card(uint32_t rgb);       // a card's fill (gradient bottom)
uint32_t cardTop(uint32_t rgb);    // the gradient's top
uint32_t cardText(uint32_t rgb);   // text on the card, 4.5:1 on cardTop
uint32_t onNavy(uint32_t rgb, double minimum = 4.5);

}  // namespace cta
