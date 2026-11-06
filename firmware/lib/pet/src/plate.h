#pragma once
// The app's pixel plates (iOS redesign revision 2: PixelShape and .plate), rasterized into ARGB8888 canvases:
// a rectangle whose corners step in whole blocks, a vertical gradient lit from the top, a rim, and a darker
// edge under it so it sits up like a game tile.
#include <stdint.h>

#include "pet_render.h"

namespace pet {

struct Shape {
    uint8_t block = 4;
    uint8_t steps[4] = {3, 1, 1, 0};   // how far each row of blocks at a corner is pulled in, in blocks
    uint8_t count = 3;
};
constexpr Shape kShapeCard{};
constexpr Shape kShapeSmall{3, {2, 1, 0, 0}, 2};   // chips, buttons, tiles
constexpr Shape kShapeChip{2, {1, 0, 0, 0}, 1};

struct PlateStyle {
    uint32_t top, bottom;      // the fill's gradient (0xRRGGBB)
    uint32_t rim = 0xFFFFFF;   // drawn at rimAlpha over the fill
    uint8_t rimAlpha = 56;     // ~22%
    uint8_t rimW = 2;
    uint8_t edge = 5;          // the plate's darker side under it, in pixels
    Shape shape = kShapeCard;
};

// How far row y (0..h-1) of a w×h shape is pulled in from each side.
int shapeInset(const Shape& s, int y, int h);
// rgb over an ARGB pixel at alpha (0-255); the result is opaque wherever either is.
uint32_t blend(uint32_t dst, uint32_t rgb, uint8_t alpha);
// A solid shape.
void fillShape(Canvas c, int x, int y, int w, int h, const Shape& s, uint32_t rgb, uint8_t alpha = 255);
// A plate: occupies (x, y) to (x + w, y + h + edge).
void drawPlate(Canvas c, int x, int y, int w, int h, const PlateStyle& p);

}  // namespace pet
