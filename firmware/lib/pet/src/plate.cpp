#include "plate.h"

namespace pet {

int shapeInset(const Shape& s, int y, int h) {
    int band = y / s.block, fromBottom = (h - 1 - y) / s.block;
    int inset = 0;
    if (band < s.count) inset = s.steps[band] * s.block;
    if (fromBottom < s.count && s.steps[fromBottom] * s.block > inset) inset = s.steps[fromBottom] * s.block;
    return inset;
}

uint32_t blend(uint32_t dst, uint32_t rgb, uint8_t a) {
    uint32_t da = dst >> 24;
    if (a == 255 || da == 0) return ((uint32_t)(da == 0 && a < 255 ? a : 255) << 24) | (rgb & 0xFFFFFF);
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t d = (dst >> shift) & 0xFF, s = (rgb >> shift) & 0xFF;
        out |= ((s * a + d * (255 - a) + 127) / 255) << shift;
    }
    return out;
}

static inline void put(Canvas c, int x, int y, uint32_t rgb, uint8_t a) {
    if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
    uint32_t& px = c.px[y * c.w + x];
    px = blend(px, rgb, a);
}

void fillShape(Canvas c, int x, int y, int w, int h, const Shape& s, uint32_t rgb, uint8_t alpha) {
    for (int j = 0; j < h; j++) {
        int in = shapeInset(s, j, h);
        for (int i = in; i < w - in; i++) put(c, x + i, y + j, rgb, alpha);
    }
}

static uint32_t mix(uint32_t a, uint32_t b, int num, int den) {
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        int va = (a >> shift) & 0xFF, vb = (b >> shift) & 0xFF;
        out |= (uint32_t)(va + (vb - va) * num / (den ? den : 1)) << shift;
    }
    return out;
}

static bool inside(const Shape& s, int i, int j, int w, int h) {
    if (i < 0 || j < 0 || i >= w || j >= h) return false;
    int in = shapeInset(s, j, h);
    return i >= in && i < w - in;
}

void drawPlate(Canvas c, int x, int y, int w, int h, const PlateStyle& p) {
    if (p.edge) {   // the side: the bottom color under 45% black
        fillShape(c, x, y + p.edge, w, h, p.shape, p.bottom);
        fillShape(c, x, y + p.edge, w, h, p.shape, 0x000000, 115);
    }
    for (int j = 0; j < h; j++) {
        uint32_t row = mix(p.top, p.bottom, j, h - 1);
        // the light from the top: white 12% fading to nothing at the middle
        uint8_t hi = j < h / 2 ? (uint8_t)(31 - 31 * j / (h / 2 ? h / 2 : 1)) : 0;
        int in = shapeInset(p.shape, j, h);
        for (int i = in; i < w - in; i++) {
            put(c, x + i, y + j, row, 255);
            if (hi) put(c, x + i, y + j, 0xFFFFFF, hi);
            bool rim = false;
            for (int r = 1; r <= p.rimW && !rim; r++)
                rim = !inside(p.shape, i - r, j, w, h) || !inside(p.shape, i + r, j, w, h) ||
                      !inside(p.shape, i, j - r, w, h) || !inside(p.shape, i, j + r, w, h);
            if (rim && p.rimAlpha) put(c, x + i, y + j, p.rim, p.rimAlpha);
        }
    }
}

}  // namespace pet
