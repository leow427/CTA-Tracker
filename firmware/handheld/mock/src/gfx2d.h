#pragma once
// A small 2D canvas over an RGB565 buffer: just the drawing the handheld mockup's HTML canvas uses (pixel plates,
// Nunito text, 5×7 pixel digits, pixel art, anti-aliased circles, strokes, and arcs). Plain C++ so the screens can
// be rendered on a laptop for previews (tools/preview.cpp) as well as on the device.
//
// A canvas covers either a whole row-major screen or one band of screen rows stored the way the panel wants it
// (rotated a quarter turn right and byte-swapped), so the device can draw a frame band by band in internal RAM
// and send each band as it is. Drawing outside the canvas's band is skipped cheaply.
#include <stddef.h>
#include <stdint.h>

namespace ui {

constexpr int W = 466, C = 233;

// 0xRRGGBB colors.
namespace col {
constexpr uint32_t bg = 0x000000, navy = 0x0b1a36, raised = 0x17294f, mist = 0x9db0d8, headlight = 0xffd95a,
                   cream = 0xf4ecc8, signal = 0x34c759, amber = 0xffb000, white = 0xffffff, dimInk = 0x5c6d93;
}

enum class Align : uint8_t { Left, Center, Right };
enum class Base : uint8_t { Top, Middle, Alphabetic };

struct Font;

struct PlateStyle {
    uint32_t fill = col::raised, fill2 = col::raised;
    uint32_t rim = col::white;
    float rimAlpha = 0.22f;
    int rimW = 2, edge = 5, block = 4;
    uint8_t steps[3] = {3, 1, 1};
    uint8_t nSteps = 3;
    bool highlight = true;
};
PlateStyle smallPlate();   // the mockup's SMALL: 3 px blocks, steps 2, 1

class Canvas {
public:
    // A whole w × h screen, row-major, native byte order.
    Canvas(uint16_t* px, int w, int h);
    // Screen rows [y0, y0 + rows) of a screenW-wide screen, laid out for the panel: rotated a quarter turn right
    // (buffer row r is screen column r, and within it screen row y is at y0 + rows - 1 - y) and byte-swapped.
    // That is exactly the panel window for columns [screenW - y0 - rows, screenW - y0), all rows.
    static Canvas panelBand(uint16_t* px, int screenW, int y0, int rows);
    // Only screen columns [x0, x1) of such a band: px holds (x1 - x0) * rows pixels, buffer row r being screen column
    // x0 + r. That is the panel window for columns [screenW - y0 - rows, screenW - y0), rows [x0, x1).
    static Canvas panelWindow(uint16_t* px, int screenW, int x0, int x1, int y0, int rows);

    // State, like ctx.save()/restore() in the mockup: a translation, a clip rectangle, and a global alpha.
    int ox = 0, oy = 0;
    float alpha = 1.f;
    void clip(int x, int y, int w, int h);   // in current (translated) coordinates, intersected with the canvas
    void resetClip();
    // True when nothing of this rectangle (current coordinates) lands inside the clip, so drawing it can be skipped.
    bool hidden(float x, float y, float w, float h) const {
        return outside(static_cast<int>(x) + ox - 1, static_cast<int>(y) + oy - 1, static_cast<int>(x + w) + ox + 2,
                       static_cast<int>(y + h) + oy + 2);
    }

    void clear(uint32_t rgb = 0);   // the whole canvas
    void fillRect(float x, float y, float w, float h, uint32_t rgb, float a = 1.f);
    void blend(int x, int y, uint32_t rgb, int a255);   // device coordinates, clipped

    // A stepped-corner pixel shape (the app's PixelShape) and a raised plate built from it.
    void fillPixelShape(int x, int y, int w, int h, int block, const uint8_t* steps, int nSteps, uint32_t rgb,
                        float a = 1.f, bool bottom = true);
    void plate(int x, int y, int w, int h, const PlateStyle& s);
    int chip(const char* text, int x, int y, uint32_t bg, uint32_t fg);   // returns its width

    // Nunito text. weight 800 or 900; sizes are the ones tools/font_build.html renders.
    static const Font* font(int weight, int size);
    static float measure(const Font* f, const char* text);
    // Whether the font has a glyph for every character (the large title sizes carry letters and digits only).
    static bool covers(const Font* f, const char* text);
    // Shortens text with "…" to fit maxW, into out (cap bytes).
    static const char* fit(const Font* f, const char* text, float maxW, char* out, size_t cap);
    float text(const Font* f, const char* text, float x, float y, uint32_t rgb, Align al = Align::Left,
               Base base = Base::Top, float a = 1.f);

    // The app's 5×7 pixel digits ("0"–"9", ":", "D", "u", "e", " "). y is the top. Returns the width.
    static int pixelWidth(const char* text, int s);
    int pixelText(const char* text, float x, int y, int s, uint32_t rgb, Align al = Align::Left);

    // Pixel art from rows of characters: any char other than '.' is drawn in rgb.
    void ink(const char* const* rows, int nRows, int x, int y, int s, uint32_t rgb, float a = 1.f);
    // An indexed sprite (pet::Sprite's layout): w × h palette indexes, 0 transparent, placed dx, dy art pixels
    // from (x, y), each art pixel an s × s block of pal565[index] (native RGB565). Same rule as pet::renderSprite.
    void sprite(const uint8_t* idx, int w, int h, int dx, int dy, int x, int y, const uint16_t* pal565, int s);

    // Anti-aliased shapes.
    void fillCircle(float cx, float cy, float r, uint32_t rgb, float a = 1.f);
    void line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, float a = 1.f);   // round caps
    // Stroke an arc of radius r from angle a0 to a1 (radians, clockwise from +x, like canvas), width lw.
    void arc(float cx, float cy, float r, float a0, float a1, float lw, uint32_t rgb, float a = 1.f,
             bool roundCaps = false);
    void arrowCircle(float cx, float cy, float r, uint32_t rgb, uint32_t inkRgb);   // arrow.right.circle.fill

    // Scaled blits of a native row-major RGB565 image centered on (cx, cy), with `key` pixels transparent and
    // global alpha a: bilinear, and nearest (rougher, much cheaper, for frames in motion). overBlack promises the
    // image lands on black, so fading it needn't read what's under it (same result, faster).
    void blitScaled(const uint16_t* src, int sw, int sh, uint16_t key, float cx, float cy, float scale, float a,
                    bool overBlack = false);
    void blitNearest(const uint16_t* src, int sw, int sh, uint16_t key, float cx, float cy, float scale, float a,
                     bool overBlack = false);

private:
    Canvas() = default;
    uint16_t& at(int x, int y) { return px_[org_ + x * sx_ + y * sy_]; }
    uint16_t enc(uint16_t c) const { return swap_ ? static_cast<uint16_t>(c << 8 | c >> 8) : c; }
    uint16_t dec(uint16_t c) const { return enc(c); }
    void fill565(int x0, int y0, int x1, int y1, uint16_t c);   // device coordinates, clipped, opaque
    bool outside(int x0, int y0, int x1, int y1) const { return x1 <= cx0_ || x0 >= cx1_ || y1 <= cy0_ || y0 >= cy1_; }

    uint16_t* px_ = nullptr;
    int org_ = 0, sx_ = 1, sy_ = 0;   // pixel (x, y) is px_[org_ + x * sx_ + y * sy_]
    bool swap_ = false;
    int bx0_ = 0, by0_ = 0, bx1_ = 0, by1_ = 0;   // what the buffer covers, device coordinates, [x0, x1)
    int cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0;   // clip, within that
};

// Optional timing of the drawing primitives (the device's "p" command): point clock at a cycle counter and each
// top-level call adds its cycles to its primitive's slot. Null (the default) costs a branch per call.
enum class Prim : uint8_t { Clear, Rect, Shape, Plate, Text, Digits, Ink, Sprite, Circle, Line, Arc, Smooth, Nearest, Count };
extern uint32_t (*gfxClock)();
extern uint32_t gfxCycles[static_cast<int>(Prim::Count)];
extern const char* const kPrimNames[static_cast<int>(Prim::Count)];

uint32_t mixToWhite(uint32_t rgb, float k);
uint32_t scaleRgb(uint32_t rgb, float k);

}  // namespace ui
