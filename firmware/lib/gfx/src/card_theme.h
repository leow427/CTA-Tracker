#pragma once
// The app's card look and the small pieces both devices draw with it (ported from the handheld's ui.cpp, which the
// dock's mockup copies): line-color card themes, text and digits in a card's colors (outlined on a bus), the live
// and alert glyphs, the 1-bit icons, Trackie, and his speech bubble.
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"
#include "gfx2d.h"

namespace ui {

// Bus cards as the app draws them (Theme.busCard, redesign revision 4): white down to a pale gray, gray text, and
// the text, icons and rim outlined in light blue. The LEDs keep cta::kBusColor.
constexpr uint32_t kBusTop = 0xFFFFFF, kBusCard = 0xEEF3F7, kBusText = 0x5B6573, kBusOutline = 0x7FCBF2;
// The arrival glyph (Theme.live / Theme.scheduled): light green when live, held still in light blue when scheduled,
// on every card and tile; on a white bus surface it wears a one-art-pixel halo in the bus's gray.
constexpr uint32_t kLiveInk = 0x7CF29A, kSchedInk = 0x9AD0FF;

// The app's LineColors math.
double lum(uint32_t rgb);
double contrast(uint32_t a, uint32_t b);
uint32_t shaded(uint32_t rgb, double target, double cap);
uint32_t mix(uint32_t rgb, double k);
uint32_t lifted(uint32_t rgb, uint32_t bg, double min, int from);

// outline: a bus's light blue around its text, icons and plates (0: none, trains); halo: the gray around the live
// glyph on a bus's white (0: none). tile, tile2: the dock's arrival tiles, top to bottom (a step darker than the
// card on a train, so they read as set into it).
struct Theme { char route[8]; bool bus; uint32_t accent, top, card, text, outline, halo, tile, tile2; };
const Theme& th(const char* route, bool bus);

// The text in the card's text color, on a bus over a light blue outline: 2 px on big text, 1 px under 20 px (as the
// app thins it on small gray letters).
float cardText(Canvas& cv, const Font* f, const char* s, float x, float y, const Theme& t, Align al = Align::Left,
               Base b = Base::Top);
// The pixel digits, likewise, with the app's 2 px outline: every pixel 2 px bigger each way, under them.
int cardDigits(Canvas& cv, const char* s, float x, int y, int scale, const Theme& t, Align al = Align::Left);
void cardArrow(Canvas& cv, float cx, float cy, float r, const Theme& t);

extern const char* const kLive[2][8];   // the arrival glyph, 8 × 8, and its blink-off frame
extern const char* const kAlert[16];    // the alert sign, 16 × 16
// A 1-bit icon by name (sun, cloud, snow, wifi, phone, off, plus, map, cards), centered on (cx, cy), s px per dot.
void icon(Canvas& cv, const char* name, float cx, float cy, int s, uint32_t rgb);
// Pixel art with, when ring isn't 0, a one-art-pixel outline: the art shifted 4 ways in ring behind it (the app's
// pixelOutline).
void inkRing(Canvas& cv, const char* const* rows, int n, int x, int y, int s, uint32_t rgb, uint32_t ring);
// The arrival glyph: light green when live (blinking while fresh), held still in light blue when scheduled; halo is
// the theme's (gray on a bus's white).
void liveGlyph(Canvas& cv, int x, int y, bool live, bool blinks, uint32_t ms, uint32_t halo);
void chevron(Canvas& cv, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb);

// Trackie at s px per art pixel, his body's top left at (x, y), with the weather accessory and (extra) his mood's
// floating extra.
void drawPet(Canvas& cv, cta::Mood mood, bool docked, int x, int y, int s, cta::Accessory acc, bool extra);

// The cream pixel speech bubble, its tail pointing left, and its width for a text.
float bubbleWidth(const char* text);
void bubble(Canvas& cv, const char* text, int x, float cy, const char* label = "TRACKIE");

}  // namespace ui
